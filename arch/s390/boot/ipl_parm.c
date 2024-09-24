// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/pgtable.h>
#include <asm/abs_lowcore.h>
#include <asm/page-states.h>
#include <asm/ebcdic.h>
#include <asm/sclp.h>
#include <asm/sections.h>
#include <asm/boot_data.h>
#include <asm/facility.h>
#include <asm/setup.h>
#include <asm/uv.h>
#include "boot.h"

struct parmarea parmarea __section(".parmarea") = {
	.kernel_version		= (unsigned long)kernel_version,
	.max_command_line_size	= COMMAND_LINE_SIZE,
	.command_line		= "root=/dev/ram0 ro",
};

char __bootdata(early_command_line)[COMMAND_LINE_SIZE];

unsigned int __bootdata_preserved(zlib_dfltcc_support) = ZLIB_DFLTCC_FULL;
struct ipl_parameter_block __bootdata_preserved(ipl_block);
int __bootdata_preserved(ipl_block_valid);
int __bootdata_preserved(__kaslr_enabled);
int __bootdata_preserved(cmma_flag) = 1;

unsigned long vmalloc_size = VMALLOC_DEFAULT_SIZE;
unsigned long memory_limit;
int vmalloc_size_set;

static inline int __diag308(unsigned long subcode, void *addr)
{
	unsigned long reg1, reg2;
	union register_pair r1;
	psw_t old;

	r1.even = (unsigned long) addr;
	r1.odd	= 0;
	asm volatile(
		"	mvc	0(16,%[psw_old]),0(%[psw_pgm])\n"
		"	epsw	%[reg1],%[reg2]\n"
		"	st	%[reg1],0(%[psw_pgm])\n"
		"	st	%[reg2],4(%[psw_pgm])\n"
		"	larl	%[reg1],1f\n"
		"	stg	%[reg1],8(%[psw_pgm])\n"
		"	diag	%[r1],%[subcode],0x308\n"
		"1:	mvc	0(16,%[psw_pgm]),0(%[psw_old])\n"
		: [r1] "+&d" (r1.pair),
		  [reg1] "=&d" (reg1),
		  [reg2] "=&a" (reg2),
		  "+Q" (get_lowcore()->program_new_psw),
		  "=Q" (old)
		: [subcode] "d" (subcode),
		  [psw_old] "a" (&old),
		  [psw_pgm] "a" (&get_lowcore()->program_new_psw)
		: "cc", "memory");
	return r1.odd;
}

void store_ipl_parmblock(void)
{
	int rc;

	rc = __diag308(DIAG308_STORE, &ipl_block);
	if (rc == DIAG308_RC_OK &&
	    ipl_block.hdr.version <= IPL_MAX_SUPPORTED_VERSION)
		ipl_block_valid = 1;
}

bool is_ipl_block_dump(void)
{
	if (ipl_block.pb0_hdr.pbt == IPL_PBT_FCP &&
	    ipl_block.fcp.opt == IPL_PB0_FCP_OPT_DUMP)
		return true;
	if (ipl_block.pb0_hdr.pbt == IPL_PBT_NVME &&
	    ipl_block.nvme.opt == IPL_PB0_NVME_OPT_DUMP)
		return true;
	if (ipl_block.pb0_hdr.pbt == IPL_PBT_ECKD &&
	    ipl_block.eckd.opt == IPL_PB0_ECKD_OPT_DUMP)
		return true;
	return false;
}

static size_t scpdata_length(const u8 *buf, size_t count)
{
	while (count) {
		if (buf[count - 1] != '\0' && buf[count - 1] != ' ')
			break;
		count--;
	}
	return count;
}

static size_t ipl_block_get_ascii_scpdata(char *dest, size_t size,
					  const struct ipl_parameter_block *ipb)
{
	const __u8 *scp_data;
	__u32 scp_data_len;
	int has_lowercase;
	size_t count = 0;
	size_t i;

	switch (ipb->pb0_hdr.pbt) {
	case IPL_PBT_FCP:
		scp_data_len = ipb->fcp.scp_data_len;
		scp_data = ipb->fcp.scp_data;
		break;
	case IPL_PBT_NVME:
		scp_data_len = ipb->nvme.scp_data_len;
		scp_data = ipb->nvme.scp_data;
		break;
	case IPL_PBT_ECKD:
		scp_data_len = ipb->eckd.scp_data_len;
		scp_data = ipb->eckd.scp_data;
		break;

	default:
		goto out;
	}

	count = min(size - 1, scpdata_length(scp_data, scp_data_len));
	if (!count)
		goto out;

	has_lowercase = 0;
	for (i = 0; i < count; i++) {
		if (!isascii(scp_data[i])) {
			count = 0;
			goto out;
		}
		if (!has_lowercase && islower(scp_data[i]))
			has_lowercase = 1;
	}

	if (has_lowercase)
		memcpy(dest, scp_data, count);
	else
		for (i = 0; i < count; i++)
			dest[i] = tolower(scp_data[i]);
out:
	dest[count] = '\0';
	return count;
}

static void append_ipl_block_parm(void)
{
	char *parm, *delim;
	size_t len, rc = 0;

	len = strlen(early_command_line);

	delim = early_command_line + len;    /* '\0' character position */
	parm = early_command_line + len + 1; /* append right after '\0' */

	switch (ipl_block.pb0_hdr.pbt) {
	case IPL_PBT_CCW:
		rc = ipl_block_get_ascii_vmparm(
			parm, COMMAND_LINE_SIZE - len - 1, &ipl_block);
		break;
	case IPL_PBT_FCP:
	case IPL_PBT_NVME:
	case IPL_PBT_ECKD:
		rc = ipl_block_get_ascii_scpdata(
			parm, COMMAND_LINE_SIZE - len - 1, &ipl_block);
		break;
	}
	if (rc) {
		if (*parm == '=')
			memmove(early_command_line, parm + 1, rc);
		else
			*delim = ' '; /* replace '\0' with space */
	}
}

static inline int has_ebcdic_char(const char *str)
{
	int i;

	for (i = 0; str[i]; i++)
		if (str[i] & 0x80)
			return 1;
	return 0;
}

void setup_boot_command_line(void)
{
	parmarea.command_line[COMMAND_LINE_SIZE - 1] = 0;
	/* convert arch command line to ascii if necessary */
	if (has_ebcdic_char(parmarea.command_line))
		EBCASC(parmarea.command_line, COMMAND_LINE_SIZE);
	/* copy arch command line */
	strcpy(early_command_line, strim(parmarea.command_line));

	/* append IPL PARM data to the boot command line */
	if (!is_prot_virt_guest() && ipl_block_valid)
		append_ipl_block_parm();
}

static void modify_facility(unsigned long nr, bool clear)
{
	if (clear)
		__clear_facility(nr, stfle_fac_list);
	else
		__set_facility(nr, stfle_fac_list);
}

static void check_cleared_facilities(void)
{
	unsigned long als[] = { FACILITIES_ALS };
	int i;

	for (i = 0; i < ARRAY_SIZE(als); i++) {
		if ((stfle_fac_list[i] & als[i]) != als[i]) {
			boot_printk("Warning: The Linux kernel requires facilities cleared via command line option\n");
			print_missing_facilities();
			break;
		}
	}
}

static void modify_fac_list(char *str)
{
	unsigned long val, endval;
	char *endp;
	bool clear;

	while (*str) {
		clear = false;
		if (*str == '!') {
			clear = true;
			str++;
		}
		val = simple_strtoull(str, &endp, 0);
		if (str == endp)
			break;
		str = endp;
		if (*str == '-') {
			str++;
			endval = simple_strtoull(str, &endp, 0);
			if (str == endp)
				break;
			str = endp;
			while (val <= endval) {
				modify_facility(val, clear);
				val++;
			}
		} else {
			modify_facility(val, clear);
		}
		if (*str != ',')
			break;
		str++;
	}
	check_cleared_facilities();
}

static char command_line_buf[COMMAND_LINE_SIZE];
void parse_boot_command_line(void)
{
	char *param, *val;
	bool enabled;
	char *args;
	int rc;

	__kaslr_enabled = IS_ENABLED(CONFIG_RANDOMIZE_BASE);
	args = strcpy(command_line_buf, early_command_line);
	while (*args) {
		args = next_arg(args, &param, &val);

		if (!strcmp(param, "mem") && val)
			memory_limit = round_down(memparse(val, NULL), PAGE_SIZE);

		if (!strcmp(param, "vmalloc") && val) {
			vmalloc_size = round_up(memparse(val, NULL), _SEGMENT_SIZE);
			vmalloc_size_set = 1;
		}

		if (!strcmp(param, "dfltcc") && val) {
			if (!strcmp(val, "off"))
				zlib_dfltcc_support = ZLIB_DFLTCC_DISABLED;
			else if (!strcmp(val, "on"))
				zlib_dfltcc_support = ZLIB_DFLTCC_FULL;
			else if (!strcmp(val, "def_only"))
				zlib_dfltcc_support = ZLIB_DFLTCC_DEFLATE_ONLY;
			else if (!strcmp(val, "inf_only"))
				zlib_dfltcc_support = ZLIB_DFLTCC_INFLATE_ONLY;
			else if (!strcmp(val, "always"))
				zlib_dfltcc_support = ZLIB_DFLTCC_FULL_DEBUG;
		}

		if (!strcmp(param, "facilities") && val)
			modify_fac_list(val);

		if (!strcmp(param, "nokaslr"))
			__kaslr_enabled = 0;

		if (!strcmp(param, "cmma")) {
			rc = kstrtobool(val, &enabled);
			if (!rc && !enabled)
				cmma_flag = 0;
		}

#if IS_ENABLED(CONFIG_KVM)
		if (!strcmp(param, "prot_virt")) {
			rc = kstrtobool(val, &enabled);
			if (!rc && enabled)
				prot_virt_host = 1;
		}
#endif
		if (!strcmp(param, "relocate_lowcore") && test_facility(193))
			relocate_lowcore = 1;
	}
}
