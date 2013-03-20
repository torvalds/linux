#ifndef __SOCFPGA_CTI_H
#define __SOCFPGA_CTI_H

#define CTI_MPU_IRQ_TRIG_IN	1
#define CTI_MPU_IRQ_TRIG_OUT	6

#define PMU_CHANNEL_0	0
#define PMU_CHANNEL_1	1

#ifdef CONFIG_HW_PERF_EVENTS
extern irqreturn_t socfpga_pmu_handler(int irq, void *dev, irq_handler_t handler);
extern int socfpga_init_cti(struct platform_device *pdev);
extern int socfpga_start_cti(struct platform_device *pdev);
extern int socfpga_stop_cti(struct platform_device *pdev);
#endif
#endif /* __SOCFPGA_CTI_H */
