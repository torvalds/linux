#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/efi.h>
#include <linux/slab.h>
#include <keys/asymmetric-type.h>
#include <keys/system_keyring.h>
#include "internal.h"

static __initdata efi_guid_t efi_cert_x509_guid = EFI_CERT_X509_GUID;
static __initdata efi_guid_t efi_cert_x509_sha256_guid = EFI_CERT_X509_SHA256_GUID;
static __initdata efi_guid_t efi_cert_sha256_guid = EFI_CERT_SHA256_GUID;

/*
 * Look to see if a UEFI variable called MokIgnoreDB exists and return true if
 * it does.
 *
 * This UEFI variable is set by the shim if a user tells the shim to not use
 * the certs/hashes in the UEFI db variable for verification purposes.  If it
 * is set, we should ignore the db variable also and the true return indicates
 * this.
 */
static __init bool uefi_check_ignore_db(void)
{
	efi_status_t status;
	unsigned int db = 0;
	unsigned long size = sizeof(db);
	efi_guid_t guid = EFI_SHIM_LOCK_GUID;

	status = efi.get_variable(L"MokIgnoreDB", &guid, NULL, &size, &db);
	return status == EFI_SUCCESS;
}

/*
 * Get a certificate list blob from the named EFI variable.
 */
static __init void *get_cert_list(efi_char16_t *name, efi_guid_t *guid,
				  unsigned long *size)
{
	efi_status_t status;
	unsigned long lsize = 4;
	unsigned long tmpdb[4];
	void *db;

	status = efi.get_variable(name, guid, NULL, &lsize, &tmpdb);
	if (status != EFI_BUFFER_TOO_SMALL) {
		pr_err("Couldn't get size: 0x%lx\n", status);
		return NULL;
	}

	db = kmalloc(lsize, GFP_KERNEL);
	if (!db) {
		pr_err("Couldn't allocate memory for uefi cert list\n");
		return NULL;
	}

	status = efi.get_variable(name, guid, NULL, &lsize, db);
	if (status != EFI_SUCCESS) {
		kfree(db);
		pr_err("Error reading db var: 0x%lx\n", status);
		return NULL;
	}

	*size = lsize;
	return db;
}

/*
 * Blacklist an X509 TBS hash.
 */
static __init void uefi_blacklist_x509_tbs(const char *source,
					   const void *data, size_t len)
{
	char *hash, *p;

	hash = kmalloc(4 + len * 2 + 1, GFP_KERNEL);
	if (!hash)
		return;
	p = memcpy(hash, "tbs:", 4);
	p += 4;
	bin2hex(p, data, len);
	p += len * 2;
	*p = 0;

	mark_hash_blacklisted(hash);
	kfree(hash);
}

/*
 * Blacklist the hash of an executable.
 */
static __init void uefi_blacklist_binary(const char *source,
					 const void *data, size_t len)
{
	char *hash, *p;

	hash = kmalloc(4 + len * 2 + 1, GFP_KERNEL);
	if (!hash)
		return;
	p = memcpy(hash, "bin:", 4);
	p += 4;
	bin2hex(p, data, len);
	p += len * 2;
	*p = 0;

	mark_hash_blacklisted(hash);
	kfree(hash);
}

/*
 * Return the appropriate handler for particular signature list types found in
 * the UEFI db and MokListRT tables.
 */
static __init efi_element_handler_t get_handler_for_db(const efi_guid_t *sig_type)
{
	if (efi_guidcmp(*sig_type, efi_cert_x509_guid) == 0)
		return add_trusted_secondary_key;
	return 0;
}

/*
 * Return the appropriate handler for particular signature list types found in
 * the UEFI dbx and MokListXRT tables.
 */
static __init efi_element_handler_t get_handler_for_dbx(const efi_guid_t *sig_type)
{
	if (efi_guidcmp(*sig_type, efi_cert_x509_sha256_guid) == 0)
		return uefi_blacklist_x509_tbs;
	if (efi_guidcmp(*sig_type, efi_cert_sha256_guid) == 0)
		return uefi_blacklist_binary;
	return 0;
}

/*
 * Load the certs contained in the UEFI databases into the secondary trusted
 * keyring and the UEFI blacklisted X.509 cert SHA256 hashes into the blacklist
 * keyring.
 */
static int __init load_uefi_certs(void)
{
	efi_guid_t secure_var = EFI_IMAGE_SECURITY_DATABASE_GUID;
	efi_guid_t mok_var = EFI_SHIM_LOCK_GUID;
	void *db = NULL, *dbx = NULL, *mok = NULL;
	unsigned long dbsize = 0, dbxsize = 0, moksize = 0;
	int rc = 0;

	if (!efi.get_variable)
		return false;

	/* Get db, MokListRT, and dbx.  They might not exist, so it isn't
	 * an error if we can't get them.
	 */
	if (!uefi_check_ignore_db()) {
		db = get_cert_list(L"db", &secure_var, &dbsize);
		if (!db) {
			pr_err("MODSIGN: Couldn't get UEFI db list\n");
		} else {
			rc = parse_efi_signature_list("UEFI:db",
						      db, dbsize, get_handler_for_db);
			if (rc)
				pr_err("Couldn't parse db signatures: %d\n", rc);
			kfree(db);
		}
	}

	mok = get_cert_list(L"MokListRT", &mok_var, &moksize);
	if (!mok) {
		pr_info("MODSIGN: Couldn't get UEFI MokListRT\n");
	} else {
		rc = parse_efi_signature_list("UEFI:MokListRT",
					      mok, moksize, get_handler_for_db);
		if (rc)
			pr_err("Couldn't parse MokListRT signatures: %d\n", rc);
		kfree(mok);
	}

	dbx = get_cert_list(L"dbx", &secure_var, &dbxsize);
	if (!dbx) {
		pr_info("MODSIGN: Couldn't get UEFI dbx list\n");
	} else {
		rc = parse_efi_signature_list("UEFI:dbx",
					      dbx, dbxsize,
					      get_handler_for_dbx);
		if (rc)
			pr_err("Couldn't parse dbx signatures: %d\n", rc);
		kfree(dbx);
	}

	return rc;
}
late_initcall(load_uefi_certs);
