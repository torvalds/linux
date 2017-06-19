/*
 * SCLP early driver
 *
 * Copyright IBM Corp. 2013
 */

#define KMSG_COMPONENT "sclp_early"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/errno.h>
#include <asm/ctl_reg.h>
#include <asm/sclp.h>
#include <asm/ipl.h>
#include "sclp_sdias.h"
#include "sclp.h"

#define SCLP_CMDW_READ_SCP_INFO		0x00020001
#define SCLP_CMDW_READ_SCP_INFO_FORCED	0x00120001

struct read_info_sccb {
	struct	sccb_header header;	/* 0-7 */
	u16	rnmax;			/* 8-9 */
	u8	rnsize;			/* 10 */
	u8	_pad_11[16 - 11];	/* 11-15 */
	u16	ncpurl;			/* 16-17 */
	u16	cpuoff;			/* 18-19 */
	u8	_pad_20[24 - 20];	/* 20-23 */
	u8	loadparm[8];		/* 24-31 */
	u8	_pad_32[42 - 32];	/* 32-41 */
	u8	fac42;			/* 42 */
	u8	fac43;			/* 43 */
	u8	_pad_44[48 - 44];	/* 44-47 */
	u64	facilities;		/* 48-55 */
	u8	_pad_56[66 - 56];	/* 56-65 */
	u8	fac66;			/* 66 */
	u8	_pad_67[76 - 67];	/* 67-83 */
	u32	ibc;			/* 76-79 */
	u8	_pad80[84 - 80];	/* 80-83 */
	u8	fac84;			/* 84 */
	u8	fac85;			/* 85 */
	u8	_pad_86[91 - 86];	/* 86-90 */
	u8	flags;			/* 91 */
	u8	_pad_92[98 - 92];	/* 92-97 */
	u8	fac98;			/* 98 */
	u8	hamaxpow;		/* 99 */
	u32	rnsize2;		/* 100-103 */
	u64	rnmax2;			/* 104-111 */
	u8	_pad_112[116 - 112];	/* 112-115 */
	u8	fac116;			/* 116 */
	u8	fac117;			/* 117 */
	u8	_pad_118;		/* 118 */
	u8	fac119;			/* 119 */
	u16	hcpua;			/* 120-121 */
	u8	_pad_122[124 - 122];	/* 122-123 */
	u32	hmfai;			/* 124-127 */
	u8	_pad_128[4096 - 128];	/* 128-4095 */
} __packed __aligned(PAGE_SIZE);

static struct sclp_ipl_info sclp_ipl_info;

struct sclp_info sclp;
EXPORT_SYMBOL(sclp);

static int __init sclp_early_read_info(struct read_info_sccb *sccb)
{
	int i;
	sclp_cmdw_t commands[] = {SCLP_CMDW_READ_SCP_INFO_FORCED,
				  SCLP_CMDW_READ_SCP_INFO};

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		memset(sccb, 0, sizeof(*sccb));
		sccb->header.length = sizeof(*sccb);
		sccb->header.function_code = 0x80;
		sccb->header.control_mask[2] = 0x80;
		if (sclp_early_cmd(commands[i], sccb))
			break;
		if (sccb->header.response_code == 0x10)
			return 0;
		if (sccb->header.response_code != 0x1f0)
			break;
	}
	return -EIO;
}

static void __init sclp_early_facilities_detect(struct read_info_sccb *sccb)
{
	struct sclp_core_entry *cpue;
	u16 boot_cpu_address, cpu;

	if (sclp_early_read_info(sccb))
		return;

	sclp.facilities = sccb->facilities;
	sclp.has_sprp = !!(sccb->fac84 & 0x02);
	sclp.has_core_type = !!(sccb->fac84 & 0x01);
	sclp.has_gsls = !!(sccb->fac85 & 0x80);
	sclp.has_64bscao = !!(sccb->fac116 & 0x80);
	sclp.has_cmma = !!(sccb->fac116 & 0x40);
	sclp.has_esca = !!(sccb->fac116 & 0x08);
	sclp.has_pfmfi = !!(sccb->fac117 & 0x40);
	sclp.has_ibs = !!(sccb->fac117 & 0x20);
	sclp.has_hvs = !!(sccb->fac119 & 0x80);
	sclp.has_kss = !!(sccb->fac98 & 0x01);
	if (sccb->fac85 & 0x02)
		S390_lowcore.machine_flags |= MACHINE_FLAG_ESOP;
	sclp.rnmax = sccb->rnmax ? sccb->rnmax : sccb->rnmax2;
	sclp.rzm = sccb->rnsize ? sccb->rnsize : sccb->rnsize2;
	sclp.rzm <<= 20;
	sclp.ibc = sccb->ibc;

	if (sccb->hamaxpow && sccb->hamaxpow < 64)
		sclp.hamax = (1UL << sccb->hamaxpow) - 1;
	else
		sclp.hamax = U64_MAX;

	if (!sccb->hcpua) {
		if (MACHINE_IS_VM)
			sclp.max_cores = 64;
		else
			sclp.max_cores = sccb->ncpurl;
	} else {
		sclp.max_cores = sccb->hcpua + 1;
	}

	boot_cpu_address = stap();
	cpue = (void *)sccb + sccb->cpuoff;
	for (cpu = 0; cpu < sccb->ncpurl; cpue++, cpu++) {
		if (boot_cpu_address != cpue->core_id)
			continue;
		sclp.has_siif = cpue->siif;
		sclp.has_sigpif = cpue->sigpif;
		sclp.has_sief2 = cpue->sief2;
		sclp.has_gpere = cpue->gpere;
		sclp.has_ib = cpue->ib;
		sclp.has_cei = cpue->cei;
		sclp.has_skey = cpue->skey;
		break;
	}

	/* Save IPL information */
	sclp_ipl_info.is_valid = 1;
	if (sccb->flags & 0x2)
		sclp_ipl_info.has_dump = 1;
	memcpy(&sclp_ipl_info.loadparm, &sccb->loadparm, LOADPARM_LEN);

	sclp.mtid = (sccb->fac42 & 0x80) ? (sccb->fac42 & 31) : 0;
	sclp.mtid_cp = (sccb->fac42 & 0x80) ? (sccb->fac43 & 31) : 0;
	sclp.mtid_prev = (sccb->fac42 & 0x80) ? (sccb->fac66 & 31) : 0;

	sclp.hmfai = sccb->hmfai;
}

/*
 * This function will be called after sclp_early_facilities_detect(), which gets
 * called from early.c code. The sclp_early_facilities_detect() function retrieves
 * and saves the IPL information.
 */
void __init sclp_early_get_ipl_info(struct sclp_ipl_info *info)
{
	*info = sclp_ipl_info;
}

static struct sclp_core_info sclp_early_core_info __initdata;
static int sclp_early_core_info_valid __initdata;

static void __init sclp_early_init_core_info(struct read_cpu_info_sccb *sccb)
{
	if (!SCLP_HAS_CPU_INFO)
		return;
	memset(sccb, 0, sizeof(*sccb));
	sccb->header.length = sizeof(*sccb);
	if (sclp_early_cmd(SCLP_CMDW_READ_CPU_INFO, sccb))
		return;
	if (sccb->header.response_code != 0x0010)
		return;
	sclp_fill_core_info(&sclp_early_core_info, sccb);
	sclp_early_core_info_valid = 1;
}

int __init sclp_early_get_core_info(struct sclp_core_info *info)
{
	if (!sclp_early_core_info_valid)
		return -EIO;
	*info = sclp_early_core_info;
	return 0;
}

static long __init sclp_early_hsa_size_init(struct sdias_sccb *sccb)
{
	memset(sccb, 0, sizeof(*sccb));
	sccb->hdr.length = sizeof(*sccb);
	sccb->evbuf.hdr.length = sizeof(struct sdias_evbuf);
	sccb->evbuf.hdr.type = EVTYP_SDIAS;
	sccb->evbuf.event_qual = SDIAS_EQ_SIZE;
	sccb->evbuf.data_id = SDIAS_DI_FCP_DUMP;
	sccb->evbuf.event_id = 4712;
	sccb->evbuf.dbs = 1;
	if (sclp_early_cmd(SCLP_CMDW_WRITE_EVENT_DATA, sccb))
		return -EIO;
	if (sccb->hdr.response_code != 0x20)
		return -EIO;
	if (sccb->evbuf.blk_cnt == 0)
		return 0;
	return (sccb->evbuf.blk_cnt - 1) * PAGE_SIZE;
}

static long __init sclp_early_hsa_copy_wait(struct sdias_sccb *sccb)
{
	memset(sccb, 0, PAGE_SIZE);
	sccb->hdr.length = PAGE_SIZE;
	if (sclp_early_cmd(SCLP_CMDW_READ_EVENT_DATA, sccb))
		return -EIO;
	if ((sccb->hdr.response_code != 0x20) && (sccb->hdr.response_code != 0x220))
		return -EIO;
	if (sccb->evbuf.blk_cnt == 0)
		return 0;
	return (sccb->evbuf.blk_cnt - 1) * PAGE_SIZE;
}

static void __init sclp_early_hsa_size_detect(void *sccb)
{
	unsigned long flags;
	long size = -EIO;

	raw_local_irq_save(flags);
	if (sclp_early_set_event_mask(sccb, EVTYP_SDIAS_MASK, EVTYP_SDIAS_MASK))
		goto out;
	size = sclp_early_hsa_size_init(sccb);
	/* First check for synchronous response (LPAR) */
	if (size)
		goto out_mask;
	if (!(S390_lowcore.ext_params & 1))
		sclp_early_wait_irq();
	size = sclp_early_hsa_copy_wait(sccb);
out_mask:
	sclp_early_set_event_mask(sccb, 0, 0);
out:
	raw_local_irq_restore(flags);
	if (size > 0)
		sclp.hsa_size = size;
}

static void __init sclp_early_console_detect(struct init_sccb *sccb)
{
	if (sccb->header.response_code != 0x20)
		return;

	if (sccb->sclp_send_mask & EVTYP_VT220MSG_MASK)
		sclp.has_vt220 = 1;

	if (sclp_early_con_check_linemode(sccb))
		sclp.has_linemode = 1;
}

void __init sclp_early_detect(void)
{
	void *sccb = &sclp_early_sccb;

	sclp_early_facilities_detect(sccb);
	sclp_early_init_core_info(sccb);
	sclp_early_hsa_size_detect(sccb);

	/*
	 * Turn off SCLP event notifications.  Also save remote masks in the
	 * sccb.  These are sufficient to detect sclp console capabilities.
	 */
	sclp_early_set_event_mask(sccb, 0, 0);
	sclp_early_console_detect(sccb);
}
