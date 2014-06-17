/*
 * sh73a0 processor support - INTC hardware block
 *
 * Copyright (C) 2010  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <mach/irqs.h>
#include <mach/sh73a0.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include "intc.h"

enum {
	UNUSED = 0,

	/* interrupt sources INTCS */
	PINTCS_PINT1, PINTCS_PINT2,
	RTDMAC_0_DEI0, RTDMAC_0_DEI1, RTDMAC_0_DEI2, RTDMAC_0_DEI3,
	CEU, MFI, BBIF2, VPU, TSIF1, _3DG_SGX543, _2DDMAC_2DDM0,
	RTDMAC_1_DEI4, RTDMAC_1_DEI5, RTDMAC_1_DADERR,
	KEYSC_KEY, VINT, MSIOF,
	TMU0_TUNI00, TMU0_TUNI01, TMU0_TUNI02,
	CMT0, TSIF0, CMT2, LMB, MSUG, MSU_MSU, MSU_MSU2,
	CTI, RWDT0, ICB, PEP, ASA, JPU_JPEG, LCDC, LCRC,
	RTDMAC_2_DEI6, RTDMAC_2_DEI7, RTDMAC_2_DEI8, RTDMAC_2_DEI9,
	RTDMAC_3_DEI10, RTDMAC_3_DEI11,
	FRC, GCU, LCDC1, CSIRX,
	DSITX0_DSITX00, DSITX0_DSITX01,
	SPU2_SPU0, SPU2_SPU1, FSI,
	TMU1_TUNI10, TMU1_TUNI11, TMU1_TUNI12,
	TSIF2, CMT4, MFIS2, CPORTS2R, TSG, DMASCH1, SCUW,
	VIO60, VIO61, CEU21, CSI21, DSITX1_DSITX10, DSITX1_DSITX11,
	DISP, DSRV, EMUX2_EMUX20I, EMUX2_EMUX21I,
	MSTIF0_MST00I, MSTIF0_MST01I, MSTIF1_MST10I, MSTIF1_MST11I,
	SPUV,

	/* interrupt groups INTCS */
	RTDMAC_0, RTDMAC_1, RTDMAC_2, RTDMAC_3,
	DSITX0, SPU2, TMU1, MSU,
};

static struct intc_vect intcs_vectors[] = {
	INTCS_VECT(PINTCS_PINT1, 0x0600), INTCS_VECT(PINTCS_PINT2, 0x0620),
	INTCS_VECT(RTDMAC_0_DEI0, 0x0800), INTCS_VECT(RTDMAC_0_DEI1, 0x0820),
	INTCS_VECT(RTDMAC_0_DEI2, 0x0840), INTCS_VECT(RTDMAC_0_DEI3, 0x0860),
	INTCS_VECT(CEU, 0x0880), INTCS_VECT(MFI, 0x0900),
	INTCS_VECT(BBIF2, 0x0960), INTCS_VECT(VPU, 0x0980),
	INTCS_VECT(TSIF1, 0x09a0), INTCS_VECT(_3DG_SGX543, 0x09e0),
	INTCS_VECT(_2DDMAC_2DDM0, 0x0a00),
	INTCS_VECT(RTDMAC_1_DEI4, 0x0b80), INTCS_VECT(RTDMAC_1_DEI5, 0x0ba0),
	INTCS_VECT(RTDMAC_1_DADERR, 0x0bc0),
	INTCS_VECT(KEYSC_KEY, 0x0be0), INTCS_VECT(VINT, 0x0c80),
	INTCS_VECT(MSIOF, 0x0d20),
	INTCS_VECT(TMU0_TUNI00, 0x0e80), INTCS_VECT(TMU0_TUNI01, 0x0ea0),
	INTCS_VECT(TMU0_TUNI02, 0x0ec0),
	INTCS_VECT(CMT0, 0x0f00), INTCS_VECT(TSIF0, 0x0f20),
	INTCS_VECT(CMT2, 0x0f40), INTCS_VECT(LMB, 0x0f60),
	INTCS_VECT(MSUG, 0x0f80),
	INTCS_VECT(MSU_MSU, 0x0fa0), INTCS_VECT(MSU_MSU2, 0x0fc0),
	INTCS_VECT(CTI, 0x0400), INTCS_VECT(RWDT0, 0x0440),
	INTCS_VECT(ICB, 0x0480), INTCS_VECT(PEP, 0x04a0),
	INTCS_VECT(ASA, 0x04c0), INTCS_VECT(JPU_JPEG, 0x0560),
	INTCS_VECT(LCDC, 0x0580), INTCS_VECT(LCRC, 0x05a0),
	INTCS_VECT(RTDMAC_2_DEI6, 0x1300), INTCS_VECT(RTDMAC_2_DEI7, 0x1320),
	INTCS_VECT(RTDMAC_2_DEI8, 0x1340), INTCS_VECT(RTDMAC_2_DEI9, 0x1360),
	INTCS_VECT(RTDMAC_3_DEI10, 0x1380), INTCS_VECT(RTDMAC_3_DEI11, 0x13a0),
	INTCS_VECT(FRC, 0x1700), INTCS_VECT(GCU, 0x1760),
	INTCS_VECT(LCDC1, 0x1780), INTCS_VECT(CSIRX, 0x17a0),
	INTCS_VECT(DSITX0_DSITX00, 0x17c0), INTCS_VECT(DSITX0_DSITX01, 0x17e0),
	INTCS_VECT(SPU2_SPU0, 0x1800), INTCS_VECT(SPU2_SPU1, 0x1820),
	INTCS_VECT(FSI, 0x1840),
	INTCS_VECT(TMU1_TUNI10, 0x1900), INTCS_VECT(TMU1_TUNI11, 0x1920),
	INTCS_VECT(TMU1_TUNI12, 0x1940),
	INTCS_VECT(TSIF2, 0x1960), INTCS_VECT(CMT4, 0x1980),
	INTCS_VECT(MFIS2, 0x1a00), INTCS_VECT(CPORTS2R, 0x1a20),
	INTCS_VECT(TSG, 0x1ae0), INTCS_VECT(DMASCH1, 0x1b00),
	INTCS_VECT(SCUW, 0x1b40),
	INTCS_VECT(VIO60, 0x1b60), INTCS_VECT(VIO61, 0x1b80),
	INTCS_VECT(CEU21, 0x1ba0), INTCS_VECT(CSI21, 0x1be0),
	INTCS_VECT(DSITX1_DSITX10, 0x1c00), INTCS_VECT(DSITX1_DSITX11, 0x1c20),
	INTCS_VECT(DISP, 0x1c40), INTCS_VECT(DSRV, 0x1c60),
	INTCS_VECT(EMUX2_EMUX20I, 0x1c80), INTCS_VECT(EMUX2_EMUX21I, 0x1ca0),
	INTCS_VECT(MSTIF0_MST00I, 0x1cc0), INTCS_VECT(MSTIF0_MST01I, 0x1ce0),
	INTCS_VECT(MSTIF1_MST10I, 0x1d00), INTCS_VECT(MSTIF1_MST11I, 0x1d20),
	INTCS_VECT(SPUV, 0x2300),
};

static struct intc_group intcs_groups[] __initdata = {
	INTC_GROUP(RTDMAC_0, RTDMAC_0_DEI0, RTDMAC_0_DEI1,
		   RTDMAC_0_DEI2, RTDMAC_0_DEI3),
	INTC_GROUP(RTDMAC_1, RTDMAC_1_DEI4, RTDMAC_1_DEI5, RTDMAC_1_DADERR),
	INTC_GROUP(RTDMAC_2, RTDMAC_2_DEI6, RTDMAC_2_DEI7,
		   RTDMAC_2_DEI8, RTDMAC_2_DEI9),
	INTC_GROUP(RTDMAC_3, RTDMAC_3_DEI10, RTDMAC_3_DEI11),
	INTC_GROUP(TMU1, TMU1_TUNI12, TMU1_TUNI11, TMU1_TUNI10),
	INTC_GROUP(DSITX0, DSITX0_DSITX00, DSITX0_DSITX01),
	INTC_GROUP(SPU2, SPU2_SPU0, SPU2_SPU1),
	INTC_GROUP(MSU, MSU_MSU, MSU_MSU2),
};

static struct intc_mask_reg intcs_mask_registers[] = {
	{ 0xffd20184, 0xffd201c4, 8, /* IMR1SA / IMCR1SA */
	  { 0, 0, 0, CEU,
	    0, 0, 0, 0 } },
	{ 0xffd20188, 0xffd201c8, 8, /* IMR2SA / IMCR2SA */
	  { 0, 0, 0, VPU,
	    BBIF2, 0, 0, MFI } },
	{ 0xffd2018c, 0xffd201cc, 8, /* IMR3SA / IMCR3SA */
	  { 0, 0, 0, _2DDMAC_2DDM0,
	    0, ASA, PEP, ICB } },
	{ 0xffd20190, 0xffd201d0, 8, /* IMR4SA / IMCR4SA */
	  { 0, 0, 0, CTI,
	    JPU_JPEG, 0, LCRC, LCDC } },
	{ 0xffd20194, 0xffd201d4, 8, /* IMR5SA / IMCR5SA */
	  { KEYSC_KEY, RTDMAC_1_DADERR, RTDMAC_1_DEI5, RTDMAC_1_DEI4,
	    RTDMAC_0_DEI3, RTDMAC_0_DEI2, RTDMAC_0_DEI1, RTDMAC_0_DEI0 } },
	{ 0xffd20198, 0xffd201d8, 8, /* IMR6SA / IMCR6SA */
	  { 0, 0, MSIOF, 0,
	    _3DG_SGX543, 0, 0, 0 } },
	{ 0xffd2019c, 0xffd201dc, 8, /* IMR7SA / IMCR7SA */
	  { 0, TMU0_TUNI02, TMU0_TUNI01, TMU0_TUNI00,
	    0, 0, 0, 0 } },
	{ 0xffd201a0, 0xffd201e0, 8, /* IMR8SA / IMCR8SA */
	  { 0, 0, 0, 0,
	    0, MSU_MSU, MSU_MSU2, MSUG } },
	{ 0xffd201a4, 0xffd201e4, 8, /* IMR9SA / IMCR9SA */
	  { 0, RWDT0, CMT2, CMT0,
	    0, 0, 0, 0 } },
	{ 0xffd201ac, 0xffd201ec, 8, /* IMR11SA / IMCR11SA */
	  { 0, 0, 0, 0,
	    0, TSIF1, LMB, TSIF0 } },
	{ 0xffd201b0, 0xffd201f0, 8, /* IMR12SA / IMCR12SA */
	  { 0, 0, 0, 0,
	    0, 0, PINTCS_PINT2, PINTCS_PINT1 } },
	{ 0xffd50180, 0xffd501c0, 8, /* IMR0SA3 / IMCR0SA3 */
	  { RTDMAC_2_DEI6, RTDMAC_2_DEI7, RTDMAC_2_DEI8, RTDMAC_2_DEI9,
	    RTDMAC_3_DEI10, RTDMAC_3_DEI11, 0, 0 } },
	{ 0xffd50190, 0xffd501d0, 8, /* IMR4SA3 / IMCR4SA3 */
	  { FRC, 0, 0, GCU,
	    LCDC1, CSIRX, DSITX0_DSITX00, DSITX0_DSITX01 } },
	{ 0xffd50194, 0xffd501d4, 8, /* IMR5SA3 / IMCR5SA3 */
	  { SPU2_SPU0, SPU2_SPU1, FSI, 0,
	    0, 0, 0, 0 } },
	{ 0xffd50198, 0xffd501d8, 8, /* IMR6SA3 / IMCR6SA3 */
	  { TMU1_TUNI10, TMU1_TUNI11, TMU1_TUNI12, 0,
	    TSIF2, CMT4, 0, 0 } },
	{ 0xffd5019c, 0xffd501dc, 8, /* IMR7SA3 / IMCR7SA3 */
	  { MFIS2, CPORTS2R, 0, 0,
	    0, 0, 0, TSG } },
	{ 0xffd501a0, 0xffd501e0, 8, /* IMR8SA3 / IMCR8SA3 */
	  { DMASCH1, 0, SCUW, VIO60,
	    VIO61, CEU21, 0, CSI21 } },
	{ 0xffd501a4, 0xffd501e4, 8, /* IMR9SA3 / IMCR9SA3 */
	  { DSITX1_DSITX10, DSITX1_DSITX11, DISP, DSRV,
	    EMUX2_EMUX20I, EMUX2_EMUX21I, MSTIF0_MST00I, MSTIF0_MST01I } },
	{ 0xffd501a8, 0xffd501e8, 8, /* IMR10SA3 / IMCR10SA3 */
	  { MSTIF0_MST00I, MSTIF0_MST01I, 0, 0,
	    0, 0, 0, 0  } },
	{ 0xffd60180, 0xffd601c0, 8, /* IMR0SA4 / IMCR0SA4 */
	  { SPUV, 0, 0, 0,
	    0, 0, 0, 0  } },
};

/* Priority is needed for INTCA to receive the INTCS interrupt */
static struct intc_prio_reg intcs_prio_registers[] = {
	{ 0xffd20000, 0, 16, 4, /* IPRAS */ { CTI, 0, _2DDMAC_2DDM0, ICB } },
	{ 0xffd20004, 0, 16, 4, /* IPRBS */ { JPU_JPEG, LCDC, 0, LCRC } },
	{ 0xffd20008, 0, 16, 4, /* IPRCS */ { BBIF2, 0, 0, 0 } },
	{ 0xffd2000c, 0, 16, 4, /* IPRDS */ { PINTCS_PINT1, PINTCS_PINT2,
					      0, 0 } },
	{ 0xffd20010, 0, 16, 4, /* IPRES */ { RTDMAC_0, CEU, MFI, VPU } },
	{ 0xffd20014, 0, 16, 4, /* IPRFS */ { KEYSC_KEY, RTDMAC_1,
					      CMT2, CMT0 } },
	{ 0xffd20018, 0, 16, 4, /* IPRGS */ { TMU0_TUNI00, TMU0_TUNI01,
					      TMU0_TUNI02, TSIF1 } },
	{ 0xffd2001c, 0, 16, 4, /* IPRHS */ { VINT, 0, 0, 0 } },
	{ 0xffd20020, 0, 16, 4, /* IPRIS */ { 0, MSIOF, TSIF0, 0 } },
	{ 0xffd20024, 0, 16, 4, /* IPRJS */ { 0, _3DG_SGX543, MSUG, MSU } },
	{ 0xffd20028, 0, 16, 4, /* IPRKS */ { 0, ASA, LMB, PEP } },
	{ 0xffd20030, 0, 16, 4, /* IPRMS */ { 0, 0, 0, RWDT0 } },
	{ 0xffd50000, 0, 16, 4, /* IPRAS3 */ { RTDMAC_2, 0, 0, 0 } },
	{ 0xffd50004, 0, 16, 4, /* IPRBS3 */ { RTDMAC_3, 0, 0, 0 } },
	{ 0xffd50020, 0, 16, 4, /* IPRIS3 */ { FRC, 0, 0, 0 } },
	{ 0xffd50024, 0, 16, 4, /* IPRJS3 */ { LCDC1, CSIRX, DSITX0, 0 } },
	{ 0xffd50028, 0, 16, 4, /* IPRKS3 */ { SPU2, 0, FSI, 0 } },
	{ 0xffd50030, 0, 16, 4, /* IPRMS3 */ { TMU1, 0, 0, TSIF2 } },
	{ 0xffd50034, 0, 16, 4, /* IPRNS3 */ { CMT4, 0, 0, 0 } },
	{ 0xffd50038, 0, 16, 4, /* IPROS3 */ { MFIS2, CPORTS2R, 0, 0 } },
	{ 0xffd50040, 0, 16, 4, /* IPRQS3 */ { DMASCH1, 0, SCUW, VIO60 } },
	{ 0xffd50044, 0, 16, 4, /* IPRRS3 */ { VIO61, CEU21, 0, CSI21 } },
	{ 0xffd50048, 0, 16, 4, /* IPRSS3 */ { DSITX1_DSITX10, DSITX1_DSITX11,
					       DISP, DSRV } },
	{ 0xffd5004c, 0, 16, 4, /* IPRTS3 */ { EMUX2_EMUX20I, EMUX2_EMUX21I,
					       MSTIF0_MST00I, MSTIF0_MST01I } },
	{ 0xffd50050, 0, 16, 4, /* IPRUS3 */ { MSTIF1_MST10I, MSTIF1_MST11I,
					       0, 0 } },
	{ 0xffd60000, 0, 16, 4, /* IPRAS4 */ { SPUV, 0, 0, 0 } },
};

static struct resource intcs_resources[] __initdata = {
	[0] = {
		.start	= 0xffd20000,
		.end	= 0xffd201ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0xffd50000,
		.end	= 0xffd501ff,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 0xffd60000,
		.end	= 0xffd601ff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct intc_desc intcs_desc __initdata = {
	.name = "sh73a0-intcs",
	.resource = intcs_resources,
	.num_resources = ARRAY_SIZE(intcs_resources),
	.hw = INTC_HW_DESC(intcs_vectors, intcs_groups, intcs_mask_registers,
			   intcs_prio_registers, NULL, NULL),
};

static struct irqaction sh73a0_intcs_cascade;

static irqreturn_t sh73a0_intcs_demux(int irq, void *dev_id)
{
	unsigned int evtcodeas = ioread32((void __iomem *)dev_id);

	generic_handle_irq(intcs_evt2irq(evtcodeas));

	return IRQ_HANDLED;
}

static int sh73a0_set_wake(struct irq_data *data, unsigned int on)
{
	return 0; /* always allow wakeup */
}

#define PINTER0_PHYS 0xe69000a0
#define PINTER1_PHYS 0xe69000a4
#define PINTER0_VIRT IOMEM(0xe69000a0)
#define PINTER1_VIRT IOMEM(0xe69000a4)
#define PINTRR0 IOMEM(0xe69000d0)
#define PINTRR1 IOMEM(0xe69000d4)

#define PINT0A_IRQ(n, irq) INTC_IRQ((n), SH73A0_PINT0_IRQ(irq))
#define PINT0B_IRQ(n, irq) INTC_IRQ((n), SH73A0_PINT0_IRQ(irq + 8))
#define PINT0C_IRQ(n, irq) INTC_IRQ((n), SH73A0_PINT0_IRQ(irq + 16))
#define PINT0D_IRQ(n, irq) INTC_IRQ((n), SH73A0_PINT0_IRQ(irq + 24))
#define PINT1E_IRQ(n, irq) INTC_IRQ((n), SH73A0_PINT1_IRQ(irq))

INTC_PINT(intc_pint0, PINTER0_PHYS, 0xe69000b0, "sh73a0-pint0",		\
  INTC_PINT_E(A), INTC_PINT_E(B), INTC_PINT_E(C), INTC_PINT_E(D),	\
  INTC_PINT_V(A, PINT0A_IRQ), INTC_PINT_V(B, PINT0B_IRQ),		\
  INTC_PINT_V(C, PINT0C_IRQ), INTC_PINT_V(D, PINT0D_IRQ),		\
  INTC_PINT_E(A), INTC_PINT_E(B), INTC_PINT_E(C), INTC_PINT_E(D),	\
  INTC_PINT_E(A), INTC_PINT_E(B), INTC_PINT_E(C), INTC_PINT_E(D));

INTC_PINT(intc_pint1, PINTER1_PHYS, 0xe69000c0, "sh73a0-pint1",		\
  INTC_PINT_E(E), INTC_PINT_E_EMPTY, INTC_PINT_E_EMPTY, INTC_PINT_E_EMPTY, \
  INTC_PINT_V(E, PINT1E_IRQ), INTC_PINT_V_NONE,				\
  INTC_PINT_V_NONE, INTC_PINT_V_NONE,					\
  INTC_PINT_E_NONE, INTC_PINT_E_NONE, INTC_PINT_E_NONE, INTC_PINT_E(E), \
  INTC_PINT_E(E), INTC_PINT_E_NONE, INTC_PINT_E_NONE, INTC_PINT_E_NONE);

static struct irqaction sh73a0_pint0_cascade;
static struct irqaction sh73a0_pint1_cascade;

static void pint_demux(void __iomem *rr, void __iomem *er, int base_irq)
{
	unsigned long value =  ioread32(rr) & ioread32(er);
	int k;

	for (k = 0; k < 32; k++) {
		if (value & (1 << (31 - k))) {
			generic_handle_irq(base_irq + k);
			iowrite32(~(1 << (31 - k)), rr);
		}
	}
}

static irqreturn_t sh73a0_pint0_demux(int irq, void *dev_id)
{
	pint_demux(PINTRR0, PINTER0_VIRT, SH73A0_PINT0_IRQ(0));
	return IRQ_HANDLED;
}

static irqreturn_t sh73a0_pint1_demux(int irq, void *dev_id)
{
	pint_demux(PINTRR1, PINTER1_VIRT, SH73A0_PINT1_IRQ(0));
	return IRQ_HANDLED;
}

void __init sh73a0_init_irq(void)
{
	void __iomem *gic_dist_base = IOMEM(0xf0001000);
	void __iomem *gic_cpu_base = IOMEM(0xf0000100);
	void __iomem *intevtsa = ioremap_nocache(0xffd20100, PAGE_SIZE);

	gic_init(0, 29, gic_dist_base, gic_cpu_base);
	gic_arch_extn.irq_set_wake = sh73a0_set_wake;

	register_intc_controller(&intcs_desc);
	register_intc_controller(&intc_pint0_desc);
	register_intc_controller(&intc_pint1_desc);

	/* demux using INTEVTSA */
	sh73a0_intcs_cascade.name = "INTCS cascade";
	sh73a0_intcs_cascade.handler = sh73a0_intcs_demux;
	sh73a0_intcs_cascade.dev_id = intevtsa;
	setup_irq(gic_spi(50), &sh73a0_intcs_cascade);

	/* PINT pins are sanely tied to the GIC as SPI */
	sh73a0_pint0_cascade.name = "PINT0 cascade";
	sh73a0_pint0_cascade.handler = sh73a0_pint0_demux;
	setup_irq(gic_spi(33), &sh73a0_pint0_cascade);

	sh73a0_pint1_cascade.name = "PINT1 cascade";
	sh73a0_pint1_cascade.handler = sh73a0_pint1_demux;
	setup_irq(gic_spi(34), &sh73a0_pint1_cascade);
}
