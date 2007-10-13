/*
 * CPM serial console support.
 *
 * Copyright 2007 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * It is assumed that the firmware (or the platform file) has already set
 * up the port.
 */

#include "types.h"
#include "io.h"
#include "ops.h"

struct cpm_scc {
	u32 gsmrl;
	u32 gsmrh;
	u16 psmr;
	u8 res1[2];
	u16 todr;
	u16 dsr;
	u16 scce;
	u8 res2[2];
	u16 sccm;
	u8 res3;
	u8 sccs;
	u8 res4[8];
};

struct cpm_smc {
	u8 res1[2];
	u16 smcmr;
	u8 res2[2];
	u8 smce;
	u8 res3[3];
	u8 smcm;
	u8 res4[5];
};

struct cpm_param {
	u16 rbase;
	u16 tbase;
	u8 rfcr;
	u8 tfcr;
};

struct cpm_bd {
	u16 sc;   /* Status and Control */
	u16 len;  /* Data length in buffer */
	u8 *addr; /* Buffer address in host memory */
};

static void *cpcr;
static struct cpm_param *param;
static struct cpm_smc *smc;
static struct cpm_scc *scc;
struct cpm_bd *tbdf, *rbdf;
static u32 cpm_cmd;
static u8 *muram_start;
static u32 muram_offset;

static void (*do_cmd)(int op);
static void (*enable_port)(void);
static void (*disable_port)(void);

#define CPM_CMD_STOP_TX     4
#define CPM_CMD_RESTART_TX  6
#define CPM_CMD_INIT_RX_TX  0

static void cpm1_cmd(int op)
{
	while (in_be16(cpcr) & 1)
		;

	out_be16(cpcr, (op << 8) | cpm_cmd | 1);

	while (in_be16(cpcr) & 1)
		;
}

static void cpm2_cmd(int op)
{
	while (in_be32(cpcr) & 0x10000)
		;

	out_be32(cpcr, op | cpm_cmd | 0x10000);

	while (in_be32(cpcr) & 0x10000)
		;
}

static void smc_disable_port(void)
{
	do_cmd(CPM_CMD_STOP_TX);
	out_be16(&smc->smcmr, in_be16(&smc->smcmr) & ~3);
}

static void scc_disable_port(void)
{
	do_cmd(CPM_CMD_STOP_TX);
	out_be32(&scc->gsmrl, in_be32(&scc->gsmrl) & ~0x30);
}

static void smc_enable_port(void)
{
	out_be16(&smc->smcmr, in_be16(&smc->smcmr) | 3);
	do_cmd(CPM_CMD_RESTART_TX);
}

static void scc_enable_port(void)
{
	out_be32(&scc->gsmrl, in_be32(&scc->gsmrl) | 0x30);
	do_cmd(CPM_CMD_RESTART_TX);
}

static int cpm_serial_open(void)
{
	disable_port();

	out_8(&param->rfcr, 0x10);
	out_8(&param->tfcr, 0x10);

	rbdf = (struct cpm_bd *)muram_start;
	rbdf->addr = (u8 *)(rbdf + 2);
	rbdf->sc = 0xa000;
	rbdf->len = 1;

	tbdf = rbdf + 1;
	tbdf->addr = (u8 *)(rbdf + 2) + 1;
	tbdf->sc = 0x2000;
	tbdf->len = 1;

	sync();
	out_be16(&param->rbase, muram_offset);
	out_be16(&param->tbase, muram_offset + sizeof(struct cpm_bd));

	do_cmd(CPM_CMD_INIT_RX_TX);

	enable_port();
	return 0;
}

static void cpm_serial_putc(unsigned char c)
{
	while (tbdf->sc & 0x8000)
		barrier();

	sync();

	tbdf->addr[0] = c;
	eieio();
	tbdf->sc |= 0x8000;
}

static unsigned char cpm_serial_tstc(void)
{
	barrier();
	return !(rbdf->sc & 0x8000);
}

static unsigned char cpm_serial_getc(void)
{
	unsigned char c;

	while (!cpm_serial_tstc())
		;

	sync();
	c = rbdf->addr[0];
	eieio();
	rbdf->sc |= 0x8000;

	return c;
}

int cpm_console_init(void *devp, struct serial_console_data *scdp)
{
	void *reg_virt[2];
	int is_smc = 0, is_cpm2 = 0, n;
	unsigned long reg_phys;
	void *parent, *muram;

	if (dt_is_compatible(devp, "fsl,cpm1-smc-uart")) {
		is_smc = 1;
	} else if (dt_is_compatible(devp, "fsl,cpm2-scc-uart")) {
		is_cpm2 = 1;
	} else if (dt_is_compatible(devp, "fsl,cpm2-smc-uart")) {
		is_cpm2 = 1;
		is_smc = 1;
	}

	if (is_smc) {
		enable_port = smc_enable_port;
		disable_port = smc_disable_port;
	} else {
		enable_port = scc_enable_port;
		disable_port = scc_disable_port;
	}

	if (is_cpm2)
		do_cmd = cpm2_cmd;
	else
		do_cmd = cpm1_cmd;

	n = getprop(devp, "fsl,cpm-command", &cpm_cmd, 4);
	if (n < 4)
		return -1;

	n = getprop(devp, "virtual-reg", reg_virt, sizeof(reg_virt));
	if (n < (int)sizeof(reg_virt)) {
		for (n = 0; n < 2; n++) {
			if (!dt_xlate_reg(devp, n, &reg_phys, NULL))
				return -1;

			reg_virt[n] = (void *)reg_phys;
		}
	}

	if (is_smc)
		smc = reg_virt[0];
	else
		scc = reg_virt[0];

	param = reg_virt[1];

	parent = get_parent(devp);
	if (!parent)
		return -1;

	n = getprop(parent, "virtual-reg", reg_virt, sizeof(reg_virt));
	if (n < (int)sizeof(reg_virt)) {
		if (!dt_xlate_reg(parent, 0, &reg_phys, NULL))
			return -1;

		reg_virt[0] = (void *)reg_phys;
	}

	cpcr = reg_virt[0];

	muram = finddevice("/soc/cpm/muram/data");
	if (!muram)
		return -1;

	/* For bootwrapper-compatible device trees, we assume that the first
	 * entry has at least 18 bytes, and that #address-cells/#data-cells
	 * is one for both parent and child.
	 */

	n = getprop(muram, "virtual-reg", reg_virt, sizeof(reg_virt));
	if (n < (int)sizeof(reg_virt)) {
		if (!dt_xlate_reg(muram, 0, &reg_phys, NULL))
			return -1;

		reg_virt[0] = (void *)reg_phys;
	}

	muram_start = reg_virt[0];

	n = getprop(muram, "reg", &muram_offset, 4);
	if (n < 4)
		return -1;

	scdp->open = cpm_serial_open;
	scdp->putc = cpm_serial_putc;
	scdp->getc = cpm_serial_getc;
	scdp->tstc = cpm_serial_tstc;

	return 0;
}
