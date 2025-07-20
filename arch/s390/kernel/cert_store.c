// SPDX-License-Identifier: GPL-2.0
/*
 * DIAG 0x320 support and certificate store handling
 *
 * Copyright IBM Corp. 2023
 * Author(s):	Anastasia Eskova <anastasia.eskova@ibm.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/key-type.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/vmalloc.h>
#include <crypto/sha2.h>
#include <keys/user-type.h>
#include <asm/debug.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include <asm/sclp.h>

#define DIAG_MAX_RETRIES		10

#define VCE_FLAGS_VALID_MASK		0x80

#define ISM_LEN_DWORDS			4
#define VCSSB_LEN_BYTES			128
#define VCSSB_LEN_NO_CERTS		4
#define VCB_LEN_NO_CERTS		64
#define VC_NAME_LEN_BYTES		64

#define CERT_STORE_KEY_TYPE_NAME	"cert_store_key"
#define CERT_STORE_KEYRING_NAME		"cert_store"

static debug_info_t *cert_store_dbf;
static debug_info_t *cert_store_hexdump;

#define pr_dbf_msg(fmt, ...) \
	debug_sprintf_event(cert_store_dbf, 3, fmt "\n", ## __VA_ARGS__)

enum diag320_subcode {
	DIAG320_SUBCODES	= 0,
	DIAG320_STORAGE		= 1,
	DIAG320_CERT_BLOCK	= 2,
};

enum diag320_rc {
	DIAG320_RC_OK		= 0x0001,
	DIAG320_RC_CS_NOMATCH	= 0x0306,
};

/* Verification Certificates Store Support Block (VCSSB). */
struct vcssb {
	u32 vcssb_length;
	u8  pad_0x04[3];
	u8  version;
	u8  pad_0x08[8];
	u32 cs_token;
	u8  pad_0x14[12];
	u16 total_vc_index_count;
	u16 max_vc_index_count;
	u8  pad_0x24[28];
	u32 max_vce_length;
	u32 max_vcxe_length;
	u8  pad_0x48[8];
	u32 max_single_vcb_length;
	u32 total_vcb_length;
	u32 max_single_vcxb_length;
	u32 total_vcxb_length;
	u8  pad_0x60[32];
} __packed __aligned(8);

/* Verification Certificate Entry (VCE) Header. */
struct vce_header {
	u32 vce_length;
	u8  flags;
	u8  key_type;
	u16 vc_index;
	u8  vc_name[VC_NAME_LEN_BYTES]; /* EBCDIC */
	u8  vc_format;
	u8  pad_0x49;
	u16 key_id_length;
	u8  pad_0x4c;
	u8  vc_hash_type;
	u16 vc_hash_length;
	u8  pad_0x50[4];
	u32 vc_length;
	u8  pad_0x58[8];
	u16 vc_hash_offset;
	u16 vc_offset;
	u8  pad_0x64[28];
} __packed __aligned(4);

/* Verification Certificate Block (VCB) Header. */
struct vcb_header {
	u32 vcb_input_length;
	u8  pad_0x04[4];
	u16 first_vc_index;
	u16 last_vc_index;
	u32 pad_0x0c;
	u32 cs_token;
	u8  pad_0x14[12];
	u32 vcb_output_length;
	u8  pad_0x24[3];
	u8  version;
	u16 stored_vc_count;
	u16 remaining_vc_count;
	u8  pad_0x2c[20];
} __packed __aligned(4);

/* Verification Certificate Block (VCB). */
struct vcb {
	struct vcb_header vcb_hdr;
	u8 vcb_buf[];
} __packed __aligned(4);

/* Verification Certificate Entry (VCE). */
struct vce {
	struct vce_header vce_hdr;
	u8 cert_data_buf[];
} __packed __aligned(4);

static void cert_store_key_describe(const struct key *key, struct seq_file *m)
{
	char ascii[VC_NAME_LEN_BYTES + 1];

	/*
	 * First 64 bytes of the key description is key name in EBCDIC CP 500.
	 * Convert it to ASCII for displaying in /proc/keys.
	 */
	strscpy(ascii, key->description);
	EBCASC_500(ascii, VC_NAME_LEN_BYTES);
	seq_puts(m, ascii);

	seq_puts(m, &key->description[VC_NAME_LEN_BYTES]);
	if (key_is_positive(key))
		seq_printf(m, ": %u", key->datalen);
}

/*
 * Certificate store key type takes over properties of
 * user key but cannot be updated.
 */
static struct key_type key_type_cert_store_key = {
	.name		= CERT_STORE_KEY_TYPE_NAME,
	.preparse	= user_preparse,
	.free_preparse	= user_free_preparse,
	.instantiate	= generic_key_instantiate,
	.revoke		= user_revoke,
	.destroy	= user_destroy,
	.describe	= cert_store_key_describe,
	.read		= user_read,
};

/* Logging functions. */
static void pr_dbf_vcb(const struct vcb *b)
{
	pr_dbf_msg("VCB Header:");
	pr_dbf_msg("vcb_input_length: %d", b->vcb_hdr.vcb_input_length);
	pr_dbf_msg("first_vc_index: %d", b->vcb_hdr.first_vc_index);
	pr_dbf_msg("last_vc_index: %d", b->vcb_hdr.last_vc_index);
	pr_dbf_msg("cs_token: %d", b->vcb_hdr.cs_token);
	pr_dbf_msg("vcb_output_length: %d", b->vcb_hdr.vcb_output_length);
	pr_dbf_msg("version: %d", b->vcb_hdr.version);
	pr_dbf_msg("stored_vc_count: %d", b->vcb_hdr.stored_vc_count);
	pr_dbf_msg("remaining_vc_count: %d", b->vcb_hdr.remaining_vc_count);
}

static void pr_dbf_vce(const struct vce *e)
{
	unsigned char vc_name[VC_NAME_LEN_BYTES + 1];
	char log_string[VC_NAME_LEN_BYTES + 40];

	pr_dbf_msg("VCE Header:");
	pr_dbf_msg("vce_hdr.vce_length: %d", e->vce_hdr.vce_length);
	pr_dbf_msg("vce_hdr.flags: %d", e->vce_hdr.flags);
	pr_dbf_msg("vce_hdr.key_type: %d", e->vce_hdr.key_type);
	pr_dbf_msg("vce_hdr.vc_index: %d", e->vce_hdr.vc_index);
	pr_dbf_msg("vce_hdr.vc_format: %d", e->vce_hdr.vc_format);
	pr_dbf_msg("vce_hdr.key_id_length: %d", e->vce_hdr.key_id_length);
	pr_dbf_msg("vce_hdr.vc_hash_type: %d", e->vce_hdr.vc_hash_type);
	pr_dbf_msg("vce_hdr.vc_hash_length: %d", e->vce_hdr.vc_hash_length);
	pr_dbf_msg("vce_hdr.vc_hash_offset: %d", e->vce_hdr.vc_hash_offset);
	pr_dbf_msg("vce_hdr.vc_length: %d", e->vce_hdr.vc_length);
	pr_dbf_msg("vce_hdr.vc_offset: %d", e->vce_hdr.vc_offset);

	/* Certificate name in ASCII. */
	memcpy(vc_name, e->vce_hdr.vc_name, VC_NAME_LEN_BYTES);
	EBCASC_500(vc_name, VC_NAME_LEN_BYTES);
	vc_name[VC_NAME_LEN_BYTES] = '\0';

	snprintf(log_string, sizeof(log_string),
		 "index: %d vce_hdr.vc_name (ASCII): %s",
		 e->vce_hdr.vc_index, vc_name);
	debug_text_event(cert_store_hexdump, 3, log_string);

	/* Certificate data. */
	debug_text_event(cert_store_hexdump, 3, "VCE: Certificate data start");
	debug_event(cert_store_hexdump, 3, (u8 *)e->cert_data_buf, 128);
	debug_text_event(cert_store_hexdump, 3, "VCE: Certificate data end");
	debug_event(cert_store_hexdump, 3,
		    (u8 *)e->cert_data_buf + e->vce_hdr.vce_length - 128, 128);
}

static void pr_dbf_vcssb(const struct vcssb *s)
{
	debug_text_event(cert_store_hexdump, 3, "DIAG320 Subcode1");
	debug_event(cert_store_hexdump, 3, (u8 *)s, VCSSB_LEN_BYTES);

	pr_dbf_msg("VCSSB:");
	pr_dbf_msg("vcssb_length: %u", s->vcssb_length);
	pr_dbf_msg("version: %u", s->version);
	pr_dbf_msg("cs_token: %u", s->cs_token);
	pr_dbf_msg("total_vc_index_count: %u", s->total_vc_index_count);
	pr_dbf_msg("max_vc_index_count: %u", s->max_vc_index_count);
	pr_dbf_msg("max_vce_length: %u", s->max_vce_length);
	pr_dbf_msg("max_vcxe_length: %u", s->max_vce_length);
	pr_dbf_msg("max_single_vcb_length: %u", s->max_single_vcb_length);
	pr_dbf_msg("total_vcb_length: %u", s->total_vcb_length);
	pr_dbf_msg("max_single_vcxb_length: %u", s->max_single_vcxb_length);
	pr_dbf_msg("total_vcxb_length: %u", s->total_vcxb_length);
}

static int __diag320(unsigned long subcode, void *addr)
{
	union register_pair rp = { .even = (unsigned long)addr, };

	asm_inline volatile(
		"	diag	%[rp],%[subcode],0x320\n"
		"0:	nopr	%%r7\n"
		EX_TABLE(0b, 0b)
		: [rp] "+d" (rp.pair)
		: [subcode] "d" (subcode)
		: "cc", "memory");

	return rp.odd;
}

static int diag320(unsigned long subcode, void *addr)
{
	diag_stat_inc(DIAG_STAT_X320);

	return __diag320(subcode, addr);
}

/*
 * Calculate SHA256 hash of the VCE certificate and compare it to hash stored in
 * VCE. Return -EINVAL if hashes don't match.
 */
static int check_certificate_hash(const struct vce *vce)
{
	u8 hash[SHA256_DIGEST_SIZE];
	u16 vc_hash_length;
	u8 *vce_hash;

	vce_hash = (u8 *)vce + vce->vce_hdr.vc_hash_offset;
	vc_hash_length = vce->vce_hdr.vc_hash_length;
	sha256((u8 *)vce + vce->vce_hdr.vc_offset, vce->vce_hdr.vc_length, hash);
	if (memcmp(vce_hash, hash, vc_hash_length) == 0)
		return 0;

	pr_dbf_msg("SHA256 hash of received certificate does not match");
	debug_text_event(cert_store_hexdump, 3, "VCE hash:");
	debug_event(cert_store_hexdump, 3, vce_hash, SHA256_DIGEST_SIZE);
	debug_text_event(cert_store_hexdump, 3, "Calculated hash:");
	debug_event(cert_store_hexdump, 3, hash, SHA256_DIGEST_SIZE);

	return -EINVAL;
}

static int check_certificate_valid(const struct vce *vce)
{
	if (!(vce->vce_hdr.flags & VCE_FLAGS_VALID_MASK)) {
		pr_dbf_msg("Certificate entry is invalid");
		return -EINVAL;
	}
	if (vce->vce_hdr.vc_format != 1) {
		pr_dbf_msg("Certificate format is not supported");
		return -EINVAL;
	}
	if (vce->vce_hdr.vc_hash_type != 1) {
		pr_dbf_msg("Hash type is not supported");
		return -EINVAL;
	}

	return check_certificate_hash(vce);
}

static struct key *get_user_session_keyring(void)
{
	key_ref_t us_keyring_ref;

	us_keyring_ref = lookup_user_key(KEY_SPEC_USER_SESSION_KEYRING,
					 KEY_LOOKUP_CREATE, KEY_NEED_LINK);
	if (IS_ERR(us_keyring_ref)) {
		pr_dbf_msg("Couldn't get user session keyring: %ld",
			   PTR_ERR(us_keyring_ref));
		return ERR_PTR(-ENOKEY);
	}
	key_ref_put(us_keyring_ref);
	return key_ref_to_ptr(us_keyring_ref);
}

/* Invalidate all keys from cert_store keyring. */
static int invalidate_keyring_keys(struct key *keyring)
{
	unsigned long num_keys, key_index;
	size_t keyring_payload_len;
	key_serial_t *key_array;
	struct key *current_key;
	int rc;

	keyring_payload_len = key_type_keyring.read(keyring, NULL, 0);
	num_keys = keyring_payload_len / sizeof(key_serial_t);
	key_array = kcalloc(num_keys, sizeof(key_serial_t), GFP_KERNEL);
	if (!key_array)
		return -ENOMEM;

	rc = key_type_keyring.read(keyring, (char *)key_array, keyring_payload_len);
	if (rc != keyring_payload_len) {
		pr_dbf_msg("Couldn't read keyring payload");
		goto out;
	}

	for (key_index = 0; key_index < num_keys; key_index++) {
		current_key = key_lookup(key_array[key_index]);
		pr_dbf_msg("Invalidating key %08x", current_key->serial);

		key_invalidate(current_key);
		key_put(current_key);
		rc = key_unlink(keyring, current_key);
		if (rc) {
			pr_dbf_msg("Couldn't unlink key %08x: %d", current_key->serial, rc);
			break;
		}
	}
out:
	kfree(key_array);
	return rc;
}

static struct key *find_cs_keyring(void)
{
	key_ref_t cs_keyring_ref;
	struct key *cs_keyring;

	cs_keyring_ref = keyring_search(make_key_ref(get_user_session_keyring(), true),
					&key_type_keyring, CERT_STORE_KEYRING_NAME,
					false);
	if (!IS_ERR(cs_keyring_ref)) {
		cs_keyring = key_ref_to_ptr(cs_keyring_ref);
		key_ref_put(cs_keyring_ref);
		goto found;
	}
	/* Search default locations: thread, process, session keyrings */
	cs_keyring = request_key(&key_type_keyring, CERT_STORE_KEYRING_NAME, NULL);
	if (IS_ERR(cs_keyring))
		return NULL;
	key_put(cs_keyring);
found:
	return cs_keyring;
}

static void cleanup_cs_keys(void)
{
	struct key *cs_keyring;

	cs_keyring = find_cs_keyring();
	if (!cs_keyring)
		return;

	pr_dbf_msg("Found cert_store keyring. Purging...");
	/*
	 * Remove cert_store_key_type in case invalidation
	 * of old cert_store keys failed (= severe error).
	 */
	if (invalidate_keyring_keys(cs_keyring))
		unregister_key_type(&key_type_cert_store_key);

	keyring_clear(cs_keyring);
	key_invalidate(cs_keyring);
	key_put(cs_keyring);
	key_unlink(get_user_session_keyring(), cs_keyring);
}

static struct key *create_cs_keyring(void)
{
	static struct key *cs_keyring;

	/* Cleanup previous cs_keyring and all associated keys if any. */
	cleanup_cs_keys();
	cs_keyring = keyring_alloc(CERT_STORE_KEYRING_NAME, GLOBAL_ROOT_UID,
				   GLOBAL_ROOT_GID, current_cred(),
				   (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_VIEW | KEY_USR_READ,
				   KEY_ALLOC_NOT_IN_QUOTA | KEY_ALLOC_SET_KEEP,
				   NULL, get_user_session_keyring());
	if (IS_ERR(cs_keyring)) {
		pr_dbf_msg("Can't allocate cert_store keyring");
		return NULL;
	}

	pr_dbf_msg("Successfully allocated cert_store keyring: %08x", cs_keyring->serial);

	/*
	 * In case a previous clean-up ran into an
	 * error and unregistered key type.
	 */
	register_key_type(&key_type_cert_store_key);

	return cs_keyring;
}

/*
 * Allocate memory and create key description in format
 * [key name in EBCDIC]:[VCE index]:[CS token].
 * Return a pointer to key description or NULL if memory
 * allocation failed. Memory should be freed by caller.
 */
static char *get_key_description(struct vcssb *vcssb, const struct vce *vce)
{
	size_t len, name_len;
	u32 cs_token;
	char *desc;

	cs_token = vcssb->cs_token;
	/* Description string contains "%64s:%05u:%010u\0". */
	name_len = sizeof(vce->vce_hdr.vc_name);
	len = name_len + 1 + 5 + 1 + 10 + 1;
	desc = kmalloc(len, GFP_KERNEL);
	if (!desc)
		return NULL;

	memcpy(desc, vce->vce_hdr.vc_name, name_len);
	snprintf(desc + name_len, len - name_len, ":%05u:%010u",
		 vce->vce_hdr.vc_index, cs_token);

	return desc;
}

/*
 * Create a key of type "cert_store_key" using the data from VCE for key
 * payload and key description. Link the key to "cert_store" keyring.
 */
static int create_key_from_vce(struct vcssb *vcssb, struct vce *vce,
			       struct key *keyring)
{
	key_ref_t newkey;
	char *desc;
	int rc;

	desc = get_key_description(vcssb, vce);
	if (!desc)
		return -ENOMEM;

	newkey = key_create_or_update(
		make_key_ref(keyring, true), CERT_STORE_KEY_TYPE_NAME,
		desc, (u8 *)vce + vce->vce_hdr.vc_offset,
		vce->vce_hdr.vc_length,
		(KEY_POS_ALL & ~KEY_POS_SETATTR)  | KEY_USR_VIEW | KEY_USR_READ,
		KEY_ALLOC_NOT_IN_QUOTA);

	rc = PTR_ERR_OR_ZERO(newkey);
	if (rc) {
		pr_dbf_msg("Couldn't create a key from Certificate Entry (%d)", rc);
		rc = -ENOKEY;
		goto out;
	}

	key_ref_put(newkey);
out:
	kfree(desc);
	return rc;
}

/* Get Verification Certificate Storage Size block with DIAG320 subcode2. */
static int get_vcssb(struct vcssb *vcssb)
{
	int diag320_rc;

	memset(vcssb, 0, sizeof(*vcssb));
	vcssb->vcssb_length = VCSSB_LEN_BYTES;
	diag320_rc = diag320(DIAG320_STORAGE, vcssb);
	pr_dbf_vcssb(vcssb);

	if (diag320_rc != DIAG320_RC_OK) {
		pr_dbf_msg("Diag 320 Subcode 1 returned bad RC: %04x", diag320_rc);
		return -EIO;
	}
	if (vcssb->vcssb_length == VCSSB_LEN_NO_CERTS) {
		pr_dbf_msg("No certificates available for current configuration");
		return -ENOKEY;
	}

	return 0;
}

static u32 get_4k_mult_vcb_size(struct vcssb *vcssb)
{
	return round_up(vcssb->max_single_vcb_length, PAGE_SIZE);
}

/* Fill input fields of single-entry VCB that will be read by LPAR. */
static void fill_vcb_input(struct vcssb *vcssb, struct vcb *vcb, u16 index)
{
	memset(vcb, 0, sizeof(*vcb));
	vcb->vcb_hdr.vcb_input_length = get_4k_mult_vcb_size(vcssb);
	vcb->vcb_hdr.cs_token = vcssb->cs_token;

	/* Request single entry. */
	vcb->vcb_hdr.first_vc_index = index;
	vcb->vcb_hdr.last_vc_index = index;
}

static void extract_vce_from_sevcb(struct vcb *vcb, struct vce *vce)
{
	struct vce *extracted_vce;

	extracted_vce = (struct vce *)vcb->vcb_buf;
	memcpy(vce, vcb->vcb_buf, extracted_vce->vce_hdr.vce_length);
	pr_dbf_vce(vce);
}

static int get_sevcb(struct vcssb *vcssb, u16 index, struct vcb *vcb)
{
	int rc, diag320_rc;

	fill_vcb_input(vcssb, vcb, index);

	diag320_rc = diag320(DIAG320_CERT_BLOCK, vcb);
	pr_dbf_msg("Diag 320 Subcode2 RC %2x", diag320_rc);
	pr_dbf_vcb(vcb);

	switch (diag320_rc) {
	case DIAG320_RC_OK:
		rc = 0;
		if (vcb->vcb_hdr.vcb_output_length == VCB_LEN_NO_CERTS) {
			pr_dbf_msg("No certificate entry for index %u", index);
			rc = -ENOKEY;
		} else if (vcb->vcb_hdr.remaining_vc_count != 0) {
			/* Retry on insufficient space. */
			pr_dbf_msg("Couldn't get all requested certificates");
			rc = -EAGAIN;
		}
		break;
	case DIAG320_RC_CS_NOMATCH:
		pr_dbf_msg("Certificate Store token mismatch");
		rc = -EAGAIN;
		break;
	default:
		pr_dbf_msg("Diag 320 Subcode2 returned bad rc (0x%4x)", diag320_rc);
		rc = -EINVAL;
		break;
	}

	return rc;
}

/*
 * Allocate memory for single-entry VCB, get VCB via DIAG320 subcode 2 call,
 * extract VCE and create a key from its' certificate.
 */
static int create_key_from_sevcb(struct vcssb *vcssb, u16 index,
				 struct key *keyring)
{
	struct vcb *vcb;
	struct vce *vce;
	int rc;

	rc = -ENOMEM;
	vcb = vmalloc(get_4k_mult_vcb_size(vcssb));
	vce = vmalloc(vcssb->max_single_vcb_length - sizeof(vcb->vcb_hdr));
	if (!vcb || !vce)
		goto out;

	rc = get_sevcb(vcssb, index, vcb);
	if (rc)
		goto out;

	extract_vce_from_sevcb(vcb, vce);
	rc = check_certificate_valid(vce);
	if (rc)
		goto out;

	rc = create_key_from_vce(vcssb, vce, keyring);
	if (rc)
		goto out;

	pr_dbf_msg("Successfully created key from Certificate Entry %d", index);
out:
	vfree(vce);
	vfree(vcb);
	return rc;
}

/*
 * Request a single-entry VCB for each VCE available for the partition.
 * Create a key from it and link it to cert_store keyring. If no keys
 * could be created (i.e. VCEs were invalid) return -ENOKEY.
 */
static int add_certificates_to_keyring(struct vcssb *vcssb, struct key *keyring)
{
	int rc, index, count, added;

	count = 0;
	added = 0;
	/* Certificate Store entries indices start with 1 and have no gaps. */
	for (index = 1; index < vcssb->total_vc_index_count + 1; index++) {
		pr_dbf_msg("Creating key from VCE %u", index);
		rc = create_key_from_sevcb(vcssb, index, keyring);
		count++;

		if (rc == -EAGAIN)
			return rc;

		if (rc)
			pr_dbf_msg("Creating key from VCE %u failed (%d)", index, rc);
		else
			added++;
	}

	if (added == 0) {
		pr_dbf_msg("Processed %d entries. No keys created", count);
		return -ENOKEY;
	}

	pr_info("Added %d of %d keys to cert_store keyring", added, count);

	/*
	 * Do not allow to link more keys to certificate store keyring after all
	 * the VCEs were processed.
	 */
	rc = keyring_restrict(make_key_ref(keyring, true), NULL, NULL);
	if (rc)
		pr_dbf_msg("Failed to set restriction to cert_store keyring (%d)", rc);

	return 0;
}

/*
 * Check which DIAG320 subcodes are installed.
 * Return -ENOENT if subcodes 1 or 2 are not available.
 */
static int query_diag320_subcodes(void)
{
	unsigned long ism[ISM_LEN_DWORDS];
	int rc;

	rc = diag320(0, ism);
	if (rc != DIAG320_RC_OK) {
		pr_dbf_msg("DIAG320 subcode query returned %04x", rc);
		return -ENOENT;
	}

	debug_text_event(cert_store_hexdump, 3, "DIAG320 Subcode 0");
	debug_event(cert_store_hexdump, 3, ism, sizeof(ism));

	if (!test_bit_inv(1, ism) || !test_bit_inv(2, ism)) {
		pr_dbf_msg("Not all required DIAG320 subcodes are installed");
		return -ENOENT;
	}

	return 0;
}

/*
 * Check if Certificate Store is supported by the firmware and DIAG320 subcodes
 * 1 and 2 are installed. Create cert_store keyring and link all certificates
 * available for the current partition to it as "cert_store_key" type
 * keys. On refresh or error invalidate cert_store keyring and destroy
 * all keys of "cert_store_key" type.
 */
static int fill_cs_keyring(void)
{
	struct key *cs_keyring;
	struct vcssb *vcssb;
	int rc;

	rc = -ENOMEM;
	vcssb = kmalloc(VCSSB_LEN_BYTES, GFP_KERNEL);
	if (!vcssb)
		goto cleanup_keys;

	rc = -ENOENT;
	if (!sclp.has_diag320) {
		pr_dbf_msg("Certificate Store is not supported");
		goto cleanup_keys;
	}

	rc = query_diag320_subcodes();
	if (rc)
		goto cleanup_keys;

	rc = get_vcssb(vcssb);
	if (rc)
		goto cleanup_keys;

	rc = -ENOMEM;
	cs_keyring = create_cs_keyring();
	if (!cs_keyring)
		goto cleanup_keys;

	rc = add_certificates_to_keyring(vcssb, cs_keyring);
	if (rc)
		goto cleanup_cs_keyring;

	goto out;

cleanup_cs_keyring:
	key_put(cs_keyring);
cleanup_keys:
	cleanup_cs_keys();
out:
	kfree(vcssb);
	return rc;
}

static DEFINE_MUTEX(cs_refresh_lock);
static int cs_status_val = -1;

static ssize_t cs_status_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	if (cs_status_val == -1)
		return sysfs_emit(buf, "uninitialized\n");
	else if (cs_status_val == 0)
		return sysfs_emit(buf, "ok\n");

	return sysfs_emit(buf, "failed (%d)\n", cs_status_val);
}

static struct kobj_attribute cs_status_attr = __ATTR_RO(cs_status);

static ssize_t refresh_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	int rc, retries;

	pr_dbf_msg("Refresh certificate store information requested");
	rc = mutex_lock_interruptible(&cs_refresh_lock);
	if (rc)
		return rc;

	for (retries = 0; retries < DIAG_MAX_RETRIES; retries++) {
		/* Request certificates from certificate store. */
		rc = fill_cs_keyring();
		if (rc)
			pr_dbf_msg("Failed to refresh certificate store information (%d)", rc);
		if (rc != -EAGAIN)
			break;
	}
	cs_status_val = rc;
	mutex_unlock(&cs_refresh_lock);

	return rc ?: count;
}

static struct kobj_attribute refresh_attr = __ATTR_WO(refresh);

static const struct attribute *cert_store_attrs[] __initconst = {
	&cs_status_attr.attr,
	&refresh_attr.attr,
	NULL,
};

static struct kobject *cert_store_kobj;

static int __init cert_store_init(void)
{
	int rc = -ENOMEM;

	cert_store_dbf = debug_register("cert_store_msg", 10, 1, 64);
	if (!cert_store_dbf)
		goto cleanup_dbf;

	cert_store_hexdump = debug_register("cert_store_hexdump", 3, 1, 128);
	if (!cert_store_hexdump)
		goto cleanup_dbf;

	debug_register_view(cert_store_hexdump, &debug_hex_ascii_view);
	debug_register_view(cert_store_dbf, &debug_sprintf_view);

	/* Create directory /sys/firmware/cert_store. */
	cert_store_kobj = kobject_create_and_add("cert_store", firmware_kobj);
	if (!cert_store_kobj)
		goto cleanup_dbf;

	rc = sysfs_create_files(cert_store_kobj, cert_store_attrs);
	if (rc)
		goto cleanup_kobj;

	register_key_type(&key_type_cert_store_key);

	return rc;

cleanup_kobj:
	kobject_put(cert_store_kobj);
cleanup_dbf:
	debug_unregister(cert_store_dbf);
	debug_unregister(cert_store_hexdump);

	return rc;
}
device_initcall(cert_store_init);
