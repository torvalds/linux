#ifndef __USBDEV_GRF_REGS_H__
#define __USBDEV_GRF_REGS_H__

typedef volatile struct tag_grf_uoc0_reg {
	/* OTG */
	u32 CON0;
	u32 CON1;
	u32 CON2;
	u32 CON3;
	u32 CON4;
	u32 CON5;
} GRF_UOC0_REG, *pGRF_UOC0_REG;

typedef volatile struct tag_grf_uoc1_reg {
	/* HOST0
	 * RK3188: DWC_OTG
	 * RK3288: OHCI & EHCI
	 */
	u32 CON0;
	u32 CON1;
	u32 CON2;
	u32 CON3;
	u32 CON4;
	u32 CON5;
} GRF_UOC1_REG, *pGRF_UOC1_REG;

typedef volatile struct tag_grf_uoc2_reg {
	/* RK3188: HISC PHY
	 * RK3288: HOST1 DWC_OTG
	 */
	u32 CON0;
	u32 CON1;
	u32 CON2;
	u32 CON3;
} GRF_UOC2_REG, *pGRF_UOC2_REG;

typedef volatile struct tag_grf_uoc3_reg {
	/* RK3188: HSIC CTLR
	 * RK3288: HSIC PHY
	 */
	u32 CON0;
	u32 CON1;
	u32 CON2;
	u32 CON3;
} GRF_UOC3_REG, *pGRF_UOC3_REG;

typedef volatile struct tag_grf_uoc4_reg {
	/* RK3288: HSIC CTLR */
	u32 CON0;
	u32 CON1;
	u32 CON2;
	u32 CON3;
} GRF_UOC4_REG, *pGRF_UOC4_REG;

typedef volatile struct tag_grf_soc_status0_rk3188 {
	unsigned reserved2:9;
	/* OTG20 */
	unsigned otg_vbusvalid:1;
	unsigned otg_bvalid:1;
	unsigned otg_linestate:2;
	unsigned otg_iddig:1;
	unsigned otg_adpsns:1;
	unsigned otg_adpprb:1;
	/* HOST20 */
	unsigned uhost_vbusvalid:1;
	unsigned uhost_bvalid:1;
	unsigned uhost_linestate:2;
	unsigned uhost_iddig:1;
	unsigned uhost_adpsns:1;
	unsigned uhost_adpprb:1;
	unsigned reserved1:9;

} GRF_SOC_STATUS_RK3188, *pGRF_SOC_STATUS_RK3188;

typedef volatile struct tag_grf_soc_status1_rk3288 {
	unsigned reserved2:16;
	unsigned hsic_ehci_usbsts:6;
	unsigned hsic_ehci_lpsmc_state:4;
	unsigned reserved1:6;

} GRF_SOC_STATUS1_RK3288, *pGRF_SOC_STATUS1_RK3288;

typedef volatile struct tag_grf_soc_status2_rk3288 {
	/* HSIC  */
	unsigned hsic_ehci_xfer_cnt:11;
	unsigned hsic_ehci_xfer_prdc:1;
	unsigned reserved2:1;
	/* OTG20  */
	unsigned otg_vbusvalid:1;
	unsigned otg_bvalid:1;
	unsigned otg_linestate:2;
	unsigned otg_iddig:1;
	/* HOST1 DWC_OTG */
	unsigned host1_chirp_on:1;
	unsigned host1_vbusvalid:1;
	unsigned host1_bvalid:1;
	unsigned host1_linestate:2;
	unsigned host1_iddig:1;
	/* HOST0 OHCI */
	unsigned host0_ohci_ccs:1;
	unsigned host0_ohci_rwe:1;
	unsigned host0_ohci_drwe:1;
	unsigned host0_linestate:2;
	unsigned host0_ohci_rmtwkp:1;
	unsigned host0_ohci_bufacc:1;
	unsigned reserved1:1;
} GRF_SOC_STATUS2_RK3288, *pGRF_SOC_STATUS2_RK3288;

typedef volatile struct tag_grf_soc_status19_rk3288 {
	unsigned host_sidle_ack:2;
	unsigned host_mstandby:1;
	unsigned host_mwakeup:1;
	unsigned host_mwait_out:1;
	unsigned host_eoi_out:2;
	unsigned host_wakeack:1;
	unsigned host_l3_ocp_mconnect:2;
	unsigned host_l3_ocp_tactive:1;
	unsigned host_l3_ocp_sconnect:3;
	unsigned reserved:9;
	/* OTG20 PHY STATUS */
	unsigned otg_chgdet:1;
	unsigned otg_fsvplus:1;
	unsigned otg_fsvminus:1;
	/* HOST0 PHY STATUS */
	unsigned host0_chgdet:1;
	unsigned host0_fsvplus:1;
	unsigned host0_fsvminus:1;
	/* HOST1 PHY STATUS */
	unsigned host1_chgdet:1;
	unsigned host1_fsvplus:1;
	unsigned host1_fsvminus:1;
} GRF_SOC_STATUS19_RK3288, *pGRF_SOC_STATUS19_RK3288;

typedef volatile struct tag_grf_soc_status21_rk3288 {
	unsigned reserved:8;
	/* HOST0 OHCI  */
	unsigned host0_ohci_globalsuspend:1;
	/* HOST0 EHCI  */
	unsigned host0_ehci_bufacc:1;
	unsigned host0_ehci_lpsmc_state:4;
	unsigned host0_ehci_xfer_prdc:1;
	unsigned host0_ehci_xfer_cnt:11;
	unsigned host0_ehci_usbsts:6;
} GRF_SOC_STATUS21_RK3288, *pGRF_SOC_STATUS21_RK3288;

#endif
