/*
 * Copyright (c) 2014 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
 * @cc_caps: chipcommon core capabilities.
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
	u32 cc_caps;
	u32 pmucaps;
	u32 pmurev;
	u32 rambase;
	u32 ramsize;
	u32 srsize;
	char name[8];
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

struct brcmf_chip *brcmf_chip_attach(void *ctx,
				     const struct brcmf_buscore_ops *ops);
void brcmf_chip_detach(struct brcmf_chip *chip);
struct brcmf_core *brcmf_chip_get_core(struct brcmf_chip *chip, u16 coreid);
struct brcmf_core *brcmf_chip_get_chipcommon(struct brcmf_chip *chip);
bool brcmf_chip_iscoreup(struct brcmf_core *core);
void brcmf_chip_coredisable(struct brcmf_core *core, u32 prereset, u32 reset);
void brcmf_chip_resetcore(struct brcmf_core *core, u32 prereset, u32 reset,
			  u32 postreset);
void brcmf_chip_set_passive(struct brcmf_chip *ci);
bool brcmf_chip_set_active(struct brcmf_chip *ci, u32 rstvec);
bool brcmf_chip_sr_capable(struct brcmf_chip *pub);

#endif /* BRCMF_AXIDMP_H */
