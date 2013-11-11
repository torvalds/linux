#ifndef __PCR_H
#define __PCR_H

struct pcr_ops {
	u64 (*read_pcr)(unsigned long);
	void (*write_pcr)(unsigned long, u64);
	u64 (*read_pic)(unsigned long);
	void (*write_pic)(unsigned long, u64);
	u64 (*nmi_picl_value)(unsigned int nmi_hz);
	u64 pcr_nmi_enable;
	u64 pcr_nmi_disable;
};
extern const struct pcr_ops *pcr_ops;

extern void deferred_pcr_work_irq(int irq, struct pt_regs *regs);
extern void schedule_deferred_pcr_work(void);

#define PCR_PIC_PRIV		0x00000001 /* PIC access is privileged */
#define PCR_STRACE		0x00000002 /* Trace supervisor events  */
#define PCR_UTRACE		0x00000004 /* Trace user events        */
#define PCR_N2_HTRACE		0x00000008 /* Trace hypervisor events  */
#define PCR_N2_TOE_OV0		0x00000010 /* Trap if PIC 0 overflows  */
#define PCR_N2_TOE_OV1		0x00000020 /* Trap if PIC 1 overflows  */
#define PCR_N2_MASK0		0x00003fc0
#define PCR_N2_MASK0_SHIFT	6
#define PCR_N2_SL0		0x0003c000
#define PCR_N2_SL0_SHIFT	14
#define PCR_N2_OV0		0x00040000
#define PCR_N2_MASK1		0x07f80000
#define PCR_N2_MASK1_SHIFT	19
#define PCR_N2_SL1		0x78000000
#define PCR_N2_SL1_SHIFT	27
#define PCR_N2_OV1		0x80000000

#define PCR_N4_OV		0x00000001 /* PIC overflow             */
#define PCR_N4_TOE		0x00000002 /* Trap On Event            */
#define PCR_N4_UTRACE		0x00000004 /* Trace user events        */
#define PCR_N4_STRACE		0x00000008 /* Trace supervisor events  */
#define PCR_N4_HTRACE		0x00000010 /* Trace hypervisor events  */
#define PCR_N4_MASK		0x000007e0 /* Event mask               */
#define PCR_N4_MASK_SHIFT	5
#define PCR_N4_SL		0x0000f800 /* Event Select             */
#define PCR_N4_SL_SHIFT		11
#define PCR_N4_PICNPT		0x00010000 /* PIC non-privileged trap  */
#define PCR_N4_PICNHT		0x00020000 /* PIC non-hypervisor trap  */
#define PCR_N4_NTC		0x00040000 /* Next-To-Commit wrap      */

extern int pcr_arch_init(void);

#endif /* __PCR_H */
