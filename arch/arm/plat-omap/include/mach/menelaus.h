/*
 * arch/arm/plat-omap/include/mach/menelaus.h
 *
 * Functions to access Menelaus power management chip
 */

#ifndef __ASM_ARCH_MENELAUS_H
#define __ASM_ARCH_MENELAUS_H

struct device;

struct menelaus_platform_data {
	int (* late_init)(struct device *dev);
};

extern int menelaus_register_mmc_callback(void (*callback)(void *data, u8 card_mask),
					  void *data);
extern void menelaus_unregister_mmc_callback(void);
extern int menelaus_set_mmc_opendrain(int slot, int enable);
extern int menelaus_set_mmc_slot(int slot, int enable, int power, int cd_on);

extern int menelaus_set_vmem(unsigned int mV);
extern int menelaus_set_vio(unsigned int mV);
extern int menelaus_set_vmmc(unsigned int mV);
extern int menelaus_set_vaux(unsigned int mV);
extern int menelaus_set_vdcdc(int dcdc, unsigned int mV);
extern int menelaus_set_slot_sel(int enable);
extern int menelaus_get_slot_pin_states(void);
extern int menelaus_set_vcore_sw(unsigned int mV);
extern int menelaus_set_vcore_hw(unsigned int roof_mV, unsigned int floor_mV);

#define EN_VPLL_SLEEP	(1 << 7)
#define EN_VMMC_SLEEP	(1 << 6)
#define EN_VAUX_SLEEP	(1 << 5)
#define EN_VIO_SLEEP	(1 << 4)
#define EN_VMEM_SLEEP	(1 << 3)
#define EN_DC3_SLEEP	(1 << 2)
#define EN_DC2_SLEEP	(1 << 1)
#define EN_VC_SLEEP	(1 << 0)

extern int menelaus_set_regulator_sleep(int enable, u32 val);

#if defined(CONFIG_ARCH_OMAP24XX) && defined(CONFIG_MENELAUS)
#define omap_has_menelaus()	1
#else
#define omap_has_menelaus()	0
#endif

#endif
