#ifndef __USBDEV_RK3190_GRF_REGS_H__
#define __USBDEV_RK3190_GRF_REGS_H__
#include <mach/grf.h>

#define GRF_REG_BASE RK319X_GRF_BASE

#define USBGRF_SOC_STATUS0	    (GRF_REG_BASE + GRF_SOC_STATUS0)
#define GRF_USBPHY1_CON_BASE    (GRF_REG_BASE + GRF_USBPHY_CON0)

#define GRF_UOC0_BASE (GRF_REG_BASE + GRF_UOC0_CON0)
#define GRF_UOC1_BASE (GRF_REG_BASE + GRF_UOC1_CON0)
#define GRF_UOC2_BASE (GRF_REG_BASE + GRF_UOC2_CON0)

typedef volatile struct tag_grf_uoc0_reg {
	u32 CON0;
	u32 CON1;
	u32 CON2;
	u32 CON3;
} GRF_UOC0_REG, *pGRF_UOC0_REG;

typedef volatile struct tag_grf_uoc1_reg {
	u32 CON0;
	u32 CON1;
} GRF_UOC1_REG, *pGRF_UOC1_REG;

typedef volatile struct tag_grf_ehci_reg {
	u32 PHY_CON0;
	u32 PHY_CON1;
	u32 CTRLER_CON0;
	u32 CTRLER_CON1;
} GRF_EHCI_REG, *pGRF_EHCI_REG;

typedef volatile struct tag_grf_usbphy_reg {
	u32 CON0;
	u32 CON1;
	u32 CON2;
	u32 CON3;
	u32 CON4;
	u32 CON5;
	u32 CON6;
	u32 CON7;
	u32 CON8;
	u32 CON9;
	u32 CON10;
	u32 CON11;
} GRF_USBPHY_REG, *pGRF_USBPHY_REG;

typedef volatile struct tag_grf_soc_status0 {
	unsigned reserved2:9;
	/* Otg20 VBus Valid */
	unsigned otg_vbusvalid:1;
	/* Otg20 BValid */
	unsigned otg_bvalid:1;
	/* Otg20 Linestate */
	unsigned otg_linestate:2;
	/* Otg20 Iddig */
	unsigned otg_iddig:1;
	/* Otg20 ADP Sense Signal */
	unsigned otg_adpsns:1;
	/* Otg20 ADP Probe Signal */
	unsigned otg_adpprb:1;
	/* Host20 VBus Valid */
	unsigned uhost_vbusvalid:1;
	/* Host20 BValid */
	unsigned uhost_bvalid:1;
	/* Host20 Linestate */
	unsigned uhost_linestate:2;
	/* Host20 Iddig */
	unsigned uhost_iddig:1;
	/* Host20 Adp sense */
	unsigned uhost_adpsns:1;
	/* Host20 Adp Probe */
	unsigned uhost_adpprb:1;
	/* INNO phy dcp detect */
	unsigned inno_dcp_det:1;
	/* INNO phy cp detect */
	unsigned inno_cp_det:1;
	/* INNO phy dp attached */
	unsigned inno_dp_attch:1;
	/* Synopsis phy BC CHGDET0 */
	unsigned snps_chgdet:1;
	/* Synopsis phy FSVMINUS */
	unsigned snps_fsvminus:1;
	/* Synopsis phy FSVPLUS */
	unsigned snps_fsvplus:1;
	unsigned reserved1:3;
} GRF_SOC_STATUS, *pGRF_SOC_STATUS;

#endif
