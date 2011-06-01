/*
 * Copyright (c) 2010 Broadcom Corporation
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

#ifndef	_NICPCI_H
#define	_NICPCI_H

struct sbpcieregs;

extern u8 pcicore_find_pci_capability(void *dev, u8 req_cap_id,
					 unsigned char *buf, u32 *buflen);
extern uint pcie_readreg(struct sbpcieregs *pcieregs,
			 uint addrtype, uint offset);
extern uint pcie_writereg(struct sbpcieregs *pcieregs,
			  uint addrtype, uint offset, uint val);

extern u8 pcie_clkreq(void *pch, u32 mask, u32 val);
extern u32 pcie_lcreg(void *pch, u32 mask, u32 val);

extern void *pcicore_init(si_t *sih, void *pdev, void *regs);
extern void pcicore_deinit(void *pch);
extern void pcicore_attach(void *pch, char *pvars, int state);
extern void pcicore_hwup(void *pch);
extern void pcicore_up(void *pch, int state);
extern void pcicore_sleep(void *pch);
extern void pcicore_down(void *pch, int state);

extern void pcie_war_ovr_aspm_update(void *pch, u8 aspm);
extern u32 pcicore_pcieserdesreg(void *pch, u32 mdioslave, u32 offset,
				    u32 mask, u32 val);

extern u32 pcicore_pciereg(void *pch, u32 offset, u32 mask,
			      u32 val, uint type);

extern bool pcicore_pmecap_fast(void *pch);
extern void pcicore_pmeen(void *pch);
extern void pcicore_pmeclr(void *pch);
extern bool pcicore_pmestat(void *pch);

#endif /* _NICPCI_H */
