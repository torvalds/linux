// SPDX-License-Identifier: GPL-2.0
/*
 * CAAM Error Reporting
 *
 * Copyright 2009-2011 Freescale Semiconductor, Inc.
 */

#include "compat.h"
#include "regs.h"
#include "desc.h"
#include "error.h"

#ifdef DEBUG
#include <linux/highmem.h>

void caam_dump_sg(const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, struct scatterlist *sg,
		  size_t tlen, bool ascii)
{
	struct scatterlist *it;
	void *it_page;
	size_t len;
	void *buf;

	for (it = sg; it && tlen > 0 ; it = sg_next(it)) {
		/*
		 * make sure the scatterlist's page
		 * has a valid virtual memory mapping
		 */
		it_page = kmap_atomic(sg_page(it));
		if (unlikely(!it_page)) {
			pr_err("caam_dump_sg: kmap failed\n");
			return;
		}

		buf = it_page + it->offset;
		len = min_t(size_t, tlen, it->length);
		print_hex_dump_debug(prefix_str, prefix_type, rowsize,
				     groupsize, buf, len, ascii);
		tlen -= len;

		kunmap_atomic(it_page);
	}
}
#else
void caam_dump_sg(const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, struct scatterlist *sg,
		  size_t tlen, bool ascii)
{}
#endif /* DEBUG */
EXPORT_SYMBOL(caam_dump_sg);

bool caam_little_end;
EXPORT_SYMBOL(caam_little_end);

bool caam_imx;
EXPORT_SYMBOL(caam_imx);

static const struct {
	u8 value;
	const char *error_text;
} desc_error_list[] = {
	{ 0x00, "No error." },
	{ 0x01, "SGT Length Error. The descriptor is trying to read more data than is contained in the SGT table." },
	{ 0x02, "SGT Null Entry Error." },
	{ 0x03, "Job Ring Control Error. There is a bad value in the Job Ring Control register." },
	{ 0x04, "Invalid Descriptor Command. The Descriptor Command field is invalid." },
	{ 0x05, "Reserved." },
	{ 0x06, "Invalid KEY Command" },
	{ 0x07, "Invalid LOAD Command" },
	{ 0x08, "Invalid STORE Command" },
	{ 0x09, "Invalid OPERATION Command" },
	{ 0x0A, "Invalid FIFO LOAD Command" },
	{ 0x0B, "Invalid FIFO STORE Command" },
	{ 0x0C, "Invalid MOVE/MOVE_LEN Command" },
	{ 0x0D, "Invalid JUMP Command. A nonlocal JUMP Command is invalid because the target is not a Job Header Command, or the jump is from a Trusted Descriptor to a Job Descriptor, or because the target Descriptor contains a Shared Descriptor." },
	{ 0x0E, "Invalid MATH Command" },
	{ 0x0F, "Invalid SIGNATURE Command" },
	{ 0x10, "Invalid Sequence Command. A SEQ IN PTR OR SEQ OUT PTR Command is invalid or a SEQ KEY, SEQ LOAD, SEQ FIFO LOAD, or SEQ FIFO STORE decremented the input or output sequence length below 0. This error may result if a built-in PROTOCOL Command has encountered a malformed PDU." },
	{ 0x11, "Skip data type invalid. The type must be 0xE or 0xF."},
	{ 0x12, "Shared Descriptor Header Error" },
	{ 0x13, "Header Error. Invalid length or parity, or certain other problems." },
	{ 0x14, "Burster Error. Burster has gotten to an illegal state" },
	{ 0x15, "Context Register Length Error. The descriptor is trying to read or write past the end of the Context Register. A SEQ LOAD or SEQ STORE with the VLF bit set was executed with too large a length in the variable length register (VSOL for SEQ STORE or VSIL for SEQ LOAD)." },
	{ 0x16, "DMA Error" },
	{ 0x17, "Reserved." },
	{ 0x1A, "Job failed due to JR reset" },
	{ 0x1B, "Job failed due to Fail Mode" },
	{ 0x1C, "DECO Watchdog timer timeout error" },
	{ 0x1D, "DECO tried to copy a key from another DECO but the other DECO's Key Registers were locked" },
	{ 0x1E, "DECO attempted to copy data from a DECO that had an unmasked Descriptor error" },
	{ 0x1F, "LIODN error. DECO was trying to share from itself or from another DECO but the two Non-SEQ LIODN values didn't match or the 'shared from' DECO's Descriptor required that the SEQ LIODNs be the same and they aren't." },
	{ 0x20, "DECO has completed a reset initiated via the DRR register" },
	{ 0x21, "Nonce error. When using EKT (CCM) key encryption option in the FIFO STORE Command, the Nonce counter reached its maximum value and this encryption mode can no longer be used." },
	{ 0x22, "Meta data is too large (> 511 bytes) for TLS decap (input frame; block ciphers) and IPsec decap (output frame, when doing the next header byte update) and DCRC (output frame)." },
	{ 0x23, "Read Input Frame error" },
	{ 0x24, "JDKEK, TDKEK or TDSK not loaded error" },
	{ 0x80, "DNR (do not run) error" },
	{ 0x81, "undefined protocol command" },
	{ 0x82, "invalid setting in PDB" },
	{ 0x83, "Anti-replay LATE error" },
	{ 0x84, "Anti-replay REPLAY error" },
	{ 0x85, "Sequence number overflow" },
	{ 0x86, "Sigver invalid signature" },
	{ 0x87, "DSA Sign Illegal test descriptor" },
	{ 0x88, "Protocol Format Error - A protocol has seen an error in the format of data received. When running RSA, this means that formatting with random padding was used, and did not follow the form: 0x00, 0x02, 8-to-N bytes of non-zero pad, 0x00, F data." },
	{ 0x89, "Protocol Size Error - A protocol has seen an error in size. When running RSA, pdb size N < (size of F) when no formatting is used; or pdb size N < (F + 11) when formatting is used." },
	{ 0xC1, "Blob Command error: Undefined mode" },
	{ 0xC2, "Blob Command error: Secure Memory Blob mode error" },
	{ 0xC4, "Blob Command error: Black Blob key or input size error" },
	{ 0xC5, "Blob Command error: Invalid key destination" },
	{ 0xC8, "Blob Command error: Trusted/Secure mode error" },
	{ 0xF0, "IPsec TTL or hop limit field either came in as 0, or was decremented to 0" },
	{ 0xF1, "3GPP HFN matches or exceeds the Threshold" },
};

static const struct {
	u8 value;
	const char *error_text;
} qi_error_list[] = {
	{ 0x1F, "Job terminated by FQ or ICID flush" },
	{ 0x20, "FD format error"},
	{ 0x21, "FD command format error"},
	{ 0x23, "FL format error"},
	{ 0x25, "CRJD specified in FD, but not enabled in FLC"},
	{ 0x30, "Max. buffer size too small"},
	{ 0x31, "DHR exceeds max. buffer size (allocate mode, S/G format)"},
	{ 0x32, "SGT exceeds max. buffer size (allocate mode, S/G format"},
	{ 0x33, "Size over/underflow (allocate mode)"},
	{ 0x34, "Size over/underflow (reuse mode)"},
	{ 0x35, "Length exceeds max. short length (allocate mode, S/G/ format)"},
	{ 0x36, "Memory footprint exceeds max. value (allocate mode, S/G/ format)"},
	{ 0x41, "SBC frame format not supported (allocate mode)"},
	{ 0x42, "Pool 0 invalid / pool 1 size < pool 0 size (allocate mode)"},
	{ 0x43, "Annotation output enabled but ASAR = 0 (allocate mode)"},
	{ 0x44, "Unsupported or reserved frame format or SGHR = 1 (reuse mode)"},
	{ 0x45, "DHR correction underflow (reuse mode, single buffer format)"},
	{ 0x46, "Annotation length exceeds offset (reuse mode)"},
	{ 0x48, "Annotation output enabled but ASA limited by ASAR (reuse mode)"},
	{ 0x49, "Data offset correction exceeds input frame data length (reuse mode)"},
	{ 0x4B, "Annotation output enabled but ASA cannot be expanded (frame list)"},
	{ 0x51, "Unsupported IF reuse mode"},
	{ 0x52, "Unsupported FL use mode"},
	{ 0x53, "Unsupported RJD use mode"},
	{ 0x54, "Unsupported inline descriptor use mode"},
	{ 0xC0, "Table buffer pool 0 depletion"},
	{ 0xC1, "Table buffer pool 1 depletion"},
	{ 0xC2, "Data buffer pool 0 depletion, no OF allocated"},
	{ 0xC3, "Data buffer pool 1 depletion, no OF allocated"},
	{ 0xC4, "Data buffer pool 0 depletion, partial OF allocated"},
	{ 0xC5, "Data buffer pool 1 depletion, partial OF allocated"},
	{ 0xD0, "FLC read error"},
	{ 0xD1, "FL read error"},
	{ 0xD2, "FL write error"},
	{ 0xD3, "OF SGT write error"},
	{ 0xD4, "PTA read error"},
	{ 0xD5, "PTA write error"},
	{ 0xD6, "OF SGT F-bit write error"},
	{ 0xD7, "ASA write error"},
	{ 0xE1, "FLC[ICR]=0 ICID error"},
	{ 0xE2, "FLC[ICR]=1 ICID error"},
	{ 0xE4, "source of ICID flush not trusted (BDI = 0)"},
};

static const char * const cha_id_list[] = {
	"",
	"AES",
	"DES",
	"ARC4",
	"MDHA",
	"RNG",
	"SNOW f8",
	"Kasumi f8/9",
	"PKHA",
	"CRCA",
	"SNOW f9",
	"ZUCE",
	"ZUCA",
};

static const char * const err_id_list[] = {
	"No error.",
	"Mode error.",
	"Data size error.",
	"Key size error.",
	"PKHA A memory size error.",
	"PKHA B memory size error.",
	"Data arrived out of sequence error.",
	"PKHA divide-by-zero error.",
	"PKHA modulus even error.",
	"DES key parity error.",
	"ICV check failed.",
	"Hardware error.",
	"Unsupported CCM AAD size.",
	"Class 1 CHA is not reset",
	"Invalid CHA combination was selected",
	"Invalid CHA selected.",
};

static const char * const rng_err_id_list[] = {
	"",
	"",
	"",
	"Instantiate",
	"Not instantiated",
	"Test instantiate",
	"Prediction resistance",
	"Prediction resistance and test request",
	"Uninstantiate",
	"Secure key generation",
};

static void report_ccb_status(struct device *jrdev, const u32 status,
			      const char *error)
{
	u8 cha_id = (status & JRSTA_CCBERR_CHAID_MASK) >>
		    JRSTA_CCBERR_CHAID_SHIFT;
	u8 err_id = status & JRSTA_CCBERR_ERRID_MASK;
	u8 idx = (status & JRSTA_DECOERR_INDEX_MASK) >>
		  JRSTA_DECOERR_INDEX_SHIFT;
	char *idx_str;
	const char *cha_str = "unidentified cha_id value 0x";
	char cha_err_code[3] = { 0 };
	const char *err_str = "unidentified err_id value 0x";
	char err_err_code[3] = { 0 };

	if (status & JRSTA_DECOERR_JUMP)
		idx_str = "jump tgt desc idx";
	else
		idx_str = "desc idx";

	if (cha_id < ARRAY_SIZE(cha_id_list))
		cha_str = cha_id_list[cha_id];
	else
		snprintf(cha_err_code, sizeof(cha_err_code), "%02x", cha_id);

	if ((cha_id << JRSTA_CCBERR_CHAID_SHIFT) == JRSTA_CCBERR_CHAID_RNG &&
	    err_id < ARRAY_SIZE(rng_err_id_list) &&
	    strlen(rng_err_id_list[err_id])) {
		/* RNG-only error */
		err_str = rng_err_id_list[err_id];
	} else {
		err_str = err_id_list[err_id];
	}

	/*
	 * CCB ICV check failures are part of normal operation life;
	 * we leave the upper layers to do what they want with them.
	 */
	if (err_id != JRSTA_CCBERR_ERRID_ICVCHK)
		dev_err(jrdev, "%08x: %s: %s %d: %s%s: %s%s\n",
			status, error, idx_str, idx,
			cha_str, cha_err_code,
			err_str, err_err_code);
}

static void report_jump_status(struct device *jrdev, const u32 status,
			       const char *error)
{
	dev_err(jrdev, "%08x: %s: %s() not implemented\n",
		status, error, __func__);
}

static void report_deco_status(struct device *jrdev, const u32 status,
			       const char *error)
{
	u8 err_id = status & JRSTA_DECOERR_ERROR_MASK;
	u8 idx = (status & JRSTA_DECOERR_INDEX_MASK) >>
		  JRSTA_DECOERR_INDEX_SHIFT;
	char *idx_str;
	const char *err_str = "unidentified error value 0x";
	char err_err_code[3] = { 0 };
	int i;

	if (status & JRSTA_DECOERR_JUMP)
		idx_str = "jump tgt desc idx";
	else
		idx_str = "desc idx";

	for (i = 0; i < ARRAY_SIZE(desc_error_list); i++)
		if (desc_error_list[i].value == err_id)
			break;

	if (i != ARRAY_SIZE(desc_error_list) && desc_error_list[i].error_text)
		err_str = desc_error_list[i].error_text;
	else
		snprintf(err_err_code, sizeof(err_err_code), "%02x", err_id);

	dev_err(jrdev, "%08x: %s: %s %d: %s%s\n",
		status, error, idx_str, idx, err_str, err_err_code);
}

static void report_qi_status(struct device *qidev, const u32 status,
			     const char *error)
{
	u8 err_id = status & JRSTA_QIERR_ERROR_MASK;
	const char *err_str = "unidentified error value 0x";
	char err_err_code[3] = { 0 };
	int i;

	for (i = 0; i < ARRAY_SIZE(qi_error_list); i++)
		if (qi_error_list[i].value == err_id)
			break;

	if (i != ARRAY_SIZE(qi_error_list) && qi_error_list[i].error_text)
		err_str = qi_error_list[i].error_text;
	else
		snprintf(err_err_code, sizeof(err_err_code), "%02x", err_id);

	dev_err(qidev, "%08x: %s: %s%s\n",
		status, error, err_str, err_err_code);
}

static void report_jr_status(struct device *jrdev, const u32 status,
			     const char *error)
{
	dev_err(jrdev, "%08x: %s: %s() not implemented\n",
		status, error, __func__);
}

static void report_cond_code_status(struct device *jrdev, const u32 status,
				    const char *error)
{
	dev_err(jrdev, "%08x: %s: %s() not implemented\n",
		status, error, __func__);
}

void caam_strstatus(struct device *jrdev, u32 status, bool qi_v2)
{
	static const struct stat_src {
		void (*report_ssed)(struct device *jrdev, const u32 status,
				    const char *error);
		const char *error;
	} status_src[16] = {
		{ NULL, "No error" },
		{ NULL, NULL },
		{ report_ccb_status, "CCB" },
		{ report_jump_status, "Jump" },
		{ report_deco_status, "DECO" },
		{ report_qi_status, "Queue Manager Interface" },
		{ report_jr_status, "Job Ring" },
		{ report_cond_code_status, "Condition Code" },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
		{ NULL, NULL },
	};
	u32 ssrc = status >> JRSTA_SSRC_SHIFT;
	const char *error = status_src[ssrc].error;

	/*
	 * If there is an error handling function, call it to report the error.
	 * Otherwise print the error source name.
	 */
	if (status_src[ssrc].report_ssed)
		status_src[ssrc].report_ssed(jrdev, status, error);
	else if (error)
		dev_err(jrdev, "%d: %s\n", ssrc, error);
	else
		dev_err(jrdev, "%d: unknown error source\n", ssrc);
}
EXPORT_SYMBOL(caam_strstatus);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FSL CAAM error reporting");
MODULE_AUTHOR("Freescale Semiconductor");
