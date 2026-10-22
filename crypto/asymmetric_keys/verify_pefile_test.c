// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the signed PE binary parser in verify_pefile.c.
 *
 * The cases build small malformed PE images in memory and call
 * verify_pefile_signature(), which parses the image before any signature
 * check, so the parser is exercised without a keyring or a real signature.
 */
#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/pe.h>
#include <linux/verification.h>

/*
 * Build a minimal PE32 image whose optional header declares @data_dirs data
 * directory entries.  header_size is sized to hold them and the image is a few
 * bytes larger.  With @data_dirs < 5 the certificate table entry (fixed index
 * 4) lies outside the declared directory.
 */
static void *build_pe32(u16 data_dirs, unsigned int *out_len)
{
	size_t cursor = sizeof(struct mz_hdr) + sizeof(struct pe_hdr) +
			sizeof(struct pe32_opt_hdr);
	size_t header_size = cursor + ((size_t)data_dirs + 1) *
				      sizeof(struct data_dirent);
	size_t pelen = header_size + 8;
	struct mz_hdr *mz;
	struct pe_hdr *pe;
	struct pe32_opt_hdr *opt;
	void *buf = kzalloc(pelen, GFP_KERNEL);

	if (!buf)
		return NULL;

	mz = buf;
	mz->magic = IMAGE_DOS_SIGNATURE;
	mz->peaddr = sizeof(struct mz_hdr);

	pe = buf + sizeof(struct mz_hdr);
	pe->magic = IMAGE_NT_SIGNATURE;

	opt = buf + sizeof(struct mz_hdr) + sizeof(struct pe_hdr);
	opt->magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	opt->header_size = header_size;
	opt->data_dirs = data_dirs;

	*out_len = pelen;
	return buf;
}

/*
 * data_dirs = 0: the certificate table entry (index 4) is absent, so the
 * parser must reject the image instead of reading ddir->certs past the end.
 */
static void pefile_missing_certs_dirent(struct kunit *test)
{
	unsigned int len;
	void *buf = build_pe32(0, &len);
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, buf);
	ret = verify_pefile_signature(buf, len, NULL,
				      VERIFYING_KEXEC_PE_SIGNATURE);
	KUNIT_EXPECT_EQ(test, ret, -ELIBBAD);
	kfree(buf);
}

/*
 * data_dirs = 5: the certificate table entry is present (and zero, i.e.
 * unsigned), so the parser gets past the directory bound and rejects the image
 * as unsigned (-ENODATA) rather than -ELIBBAD.
 */
static void pefile_present_certs_dirent(struct kunit *test)
{
	unsigned int len;
	void *buf = build_pe32(5, &len);
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, buf);
	ret = verify_pefile_signature(buf, len, NULL,
				      VERIFYING_KEXEC_PE_SIGNATURE);
	KUNIT_EXPECT_EQ(test, ret, -ENODATA);
	kfree(buf);
}

static struct kunit_case verify_pefile_cases[] = {
	KUNIT_CASE(pefile_missing_certs_dirent),
	KUNIT_CASE(pefile_present_certs_dirent),
	{}
};

static struct kunit_suite verify_pefile_suite = {
	.name = "verify_pefile",
	.test_cases = verify_pefile_cases,
};

kunit_test_suite(verify_pefile_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for the signed PE binary parser");
