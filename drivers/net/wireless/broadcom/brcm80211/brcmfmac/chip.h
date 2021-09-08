// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#ifndef BRCMF_CHIP_H
#define BRCMF_CHIP_H

#include <linux/types.h>

#define CORE_CC_REG(base, field) \
		(base + offsetof(struct chipcregs, field))

/**
 * struct brcmf_chip - chip level information.
 *
 * @chip: chip identifier.
 * @chiprev: chip revision.
 * @enum_base: base address of core enumeration space.
 * @cc_caps: chipcommon core capabilities.
 * @cc_caps_ext: chipcommon core extended capabilities.
 * @pmucaps: PMU capabilities.
 * @pmurev: PMU revision.
 * @rambase: RAM base address (only applicable for ARM CR4 chips).
 * @ramsize: amount of RAM on chip including retention.
 * @srsize: amount of retention RAM on chip.
 * @name: string representation of the chip identifier.
 */
struct brcmf_chip {
	u32 chip;
	u32 chiprev;
	u32 enum_base;
	u32 cc_caps;
	u32 cc_caps_ext;
	u32 pmucaps;
	u32 pmurev;
	u32 rambase;
	u32 ramsize;
	u32 srsize;
	char name[12];
};

/**
 * struct brcmf_core - core related information.
 *
 * @id: core identifier.
 * @rev: core revision.
 * @base: base address of core register space.
 */
struct brcmf_core {
	u16 id;
	u16 rev;
	u32 base;
};

/**
 * struct brcmf_buscore_ops - buscore specific callbacks.
 *
 * @read32: read 32-bit value over bus.
 * @write32: write 32-bit value over bus.
 * @prepare: prepare bus for core configuration.
 * @setup: bus-specific core setup.
 * @active: chip becomes active.
 *	The callback should use the provided @rstvec when non-zero.
 */
struct brcmf_buscore_ops {
	u32 (*read32)(void *ctx, u32 addr);
	void (*write32)(void *ctx, u32 addr, u32 value);
	int (*prepare)(void *ctx);
	int (*reset)(void *ctx, struct brcmf_chip *chip);
	int (*setup)(void *ctx, struct brcmf_chip *chip);
	void (*activate)(void *ctx, struct brcmf_chip *chip, u32 rstvec);
};

int brcmf_chip_get_raminfo(struct brcmf_chip *pub);
struct brcmf_chip *brcmf_chip_attach(void *ctx, u16 devid,
				     const struct brcmf_buscore_ops *ops);
void brcmf_chip_detach(struct brcmf_chip *chip);
struct brcmf_core *brcmf_chip_get_core(struct brcmf_chip *chip, u16 coreid);
struct brcmf_core *brcmf_chip_get_d11core(struct brcmf_chip *pub, u8 unit);
struct brcmf_core *brcmf_chip_get_chipcommon(struct brcmf_chip *chip);
struct brcmf_core *brcmf_chip_get_pmu(struct brcmf_chip *pub);
bool brcmf_chip_iscoreup(struct brcmf_core *core);
void brcmf_chip_coredisable(struct brcmf_core *core, u32 prereset, u32 reset);
void brcmf_chip_resetcore(struct brcmf_core *core, u32 prereset, u32 reset,
			  u32 postreset);
void brcmf_chip_set_passive(struct brcmf_chip *ci);
bool brcmf_chip_set_active(struct brcmf_chip *ci, u32 rstvec);
bool brcmf_chip_sr_capable(struct brcmf_chip *pub);
char *brcmf_chip_name(u32 chipid, u32 chiprev, char *buf, uint len);
u32 brcmf_chip_enum_base(u16 devid);

#endif /* BRCMF_AXIDMP_H */
