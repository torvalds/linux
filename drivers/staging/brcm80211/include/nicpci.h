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

#if defined(BCMSDIO) || (defined(BCMBUSTYPE) && (BCMBUSTYPE == SI_BUS))
#define pcicore_find_pci_capability(a, b, c, d) (0)
#define pcie_readreg(a, b, c, d) (0)
#define pcie_writereg(a, b, c, d, e) (0)

#define pcie_clkreq(a, b, c)	(0)
#define pcie_lcreg(a, b, c)	(0)

#define pcicore_init(a, b, c) (0x0dadbeef)
#define pcicore_deinit(a)	do { } while (0)
#define pcicore_attach(a, b, c)	do { } while (0)
#define pcicore_hwup(a)		do { } while (0)
#define pcicore_up(a, b)	do { } while (0)
#define pcicore_sleep(a)	do { } while (0)
#define pcicore_down(a, b)	do { } while (0)

#define pcie_war_ovr_aspm_update(a, b)	do { } while (0)

#define pcicore_pcieserdesreg(a, b, c, d, e) (0)
#define pcicore_pciereg(a, b, c, d, e) (0)

#define pcicore_pmecap_fast(a)	(FALSE)
#define pcicore_pmeen(a)	do { } while (0)
#define pcicore_pmeclr(a)	do { } while (0)
#define pcicore_pmestat(a)	(FALSE)
#else
struct sbpcieregs;

extern u8 pcicore_find_pci_capability(osl_t *osh, u8 req_cap_id,
					 uchar *buf, uint32 *buflen);
extern uint pcie_readreg(osl_t *osh, struct sbpcieregs *pcieregs,
			 uint addrtype, uint offset);
extern uint pcie_writereg(osl_t *osh, struct sbpcieregs *pcieregs,
			  uint addrtype, uint offset, uint val);

extern u8 pcie_clkreq(void *pch, uint32 mask, uint32 val);
extern uint32 pcie_lcreg(void *pch, uint32 mask, uint32 val);

extern void *pcicore_init(si_t *sih, osl_t *osh, void *regs);
extern void pcicore_deinit(void *pch);
extern void pcicore_attach(void *pch, char *pvars, int state);
extern void pcicore_hwup(void *pch);
extern void pcicore_up(void *pch, int state);
extern void pcicore_sleep(void *pch);
extern void pcicore_down(void *pch, int state);

extern void pcie_war_ovr_aspm_update(void *pch, u8 aspm);
extern uint32 pcicore_pcieserdesreg(void *pch, uint32 mdioslave, uint32 offset,
				    uint32 mask, uint32 val);

extern uint32 pcicore_pciereg(void *pch, uint32 offset, uint32 mask,
			      uint32 val, uint type);

extern bool pcicore_pmecap_fast(osl_t *osh);
extern void pcicore_pmeen(void *pch);
extern void pcicore_pmeclr(void *pch);
extern bool pcicore_pmestat(void *pch);
#endif				/* defined(BCMSDIO) || (defined(BCMBUSTYPE) && (BCMBUSTYPE == SI_BUS)) */

#endif				/* _NICPCI_H */
