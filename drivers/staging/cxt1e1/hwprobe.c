/* Copyright (C) 2007  One Stop Systems
 * Copyright (C) 2003-2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/netdevice.h>
#include <linux/hdlc.h>
#include <linux/if_arp.h>
#include <asm/uaccess.h>
#include <linux/rtnetlink.h>
#include <linux/pci.h>
#include "pmcc4_sysdep.h"
#include "sbecom_inline_linux.h"
#include "libsbew.h"
#include "pmcc4_private.h"
#include "pmcc4.h"
#include "pmcc4_ioctls.h"
#include "pmc93x6_eeprom.h"
#ifdef CONFIG_PROC_FS
#include "sbeproc.h"
#endif

#ifdef SBE_INCLUDE_SYMBOLS
#define STATIC
#else
#define STATIC  static
#endif

extern int  cxt1e1_log_level;
extern int  error_flag;
extern int  drvr_state;

/* forward references */
void        c4_stopwd (ci_t *);
struct net_device * __init c4_add_dev (hdw_info_t *, int, unsigned long, unsigned long, int, int);


struct s_hdw_info hdw_info[MAX_BOARDS];


void        __init
show_two (hdw_info_t * hi, int brdno)
{
    ci_t       *ci;
    struct pci_dev *pdev;
    char       *bid;
    char       *bp, banner[80];
    char        sn[6];

    bp = banner;
    memset (banner, 0, 80);         /* clear print buffer */

    ci = (ci_t *)(netdev_priv(hi->ndev));
    bid = sbeid_get_bdname (ci);
    switch (hi->promfmt)
    {
    case PROM_FORMAT_TYPE1:
        memcpy (sn, (FLD_TYPE1 *) (hi->mfg_info.pft1.Serial), 6);
        break;
    case PROM_FORMAT_TYPE2:
        memcpy (sn, (FLD_TYPE2 *) (hi->mfg_info.pft2.Serial), 6);
        break;
    default:
        memset (sn, 0, 6);
        break;
    }

    sprintf (banner, "%s: %s  S/N %06X, MUSYCC Rev %02X",
             hi->devname, bid,
             ((sn[3] << 16) & 0xff0000) |
              ((sn[4] << 8) & 0x00ff00) |
              (sn[5] & 0x0000ff),
             (u_int8_t) hi->revid[0]);

    pr_info("%s\n", banner);

    pdev = hi->pdev[0];
    pr_info("%s: %s at v/p=%lx/%lx (%02x:%02x.%x) irq %d\n",
            hi->devname, "MUSYCC",
            (unsigned long) hi->addr_mapped[0], hi->addr[0],
            hi->pci_busno, (u_int8_t) PCI_SLOT (pdev->devfn),
            (u_int8_t) PCI_FUNC (pdev->devfn), pdev->irq);

    pdev = hi->pdev[1];
    pr_info("%s: %s at v/p=%lx/%lx (%02x:%02x.%x) irq %d\n",
            hi->devname, "EBUS  ",
            (unsigned long) hi->addr_mapped[1], hi->addr[1],
            hi->pci_busno, (u_int8_t) PCI_SLOT (pdev->devfn),
            (u_int8_t) PCI_FUNC (pdev->devfn), pdev->irq);
}


void        __init
hdw_sn_get (hdw_info_t * hi, int brdno)
{
    /* obtain hardware EEPROM information */
    long        addr;

    addr = (long) hi->addr_mapped[1] + EEPROM_OFFSET;

    /* read EEPROM with largest known format size... */
    pmc_eeprom_read_buffer (addr, 0, (char *) hi->mfg_info.data, sizeof (FLD_TYPE2));

#if 0
    {
        unsigned char *ucp = (unsigned char *) &hi->mfg_info.data;

        pr_info("eeprom[00]:  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
                *(ucp + 0), *(ucp + 1), *(ucp + 2), *(ucp + 3), *(ucp + 4), *(ucp + 5), *(ucp + 6), *(ucp + 7));
        pr_info("eeprom[08]:  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
                *(ucp + 8), *(ucp + 9), *(ucp + 10), *(ucp + 11), *(ucp + 12), *(ucp + 13), *(ucp + 14), *(ucp + 15));
        pr_info("eeprom[16]:  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
                *(ucp + 16), *(ucp + 17), *(ucp + 18), *(ucp + 19), *(ucp + 20), *(ucp + 21), *(ucp + 22), *(ucp + 23));
        pr_info("eeprom[24]:  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
                *(ucp + 24), *(ucp + 25), *(ucp + 26), *(ucp + 27), *(ucp + 28), *(ucp + 29), *(ucp + 30), *(ucp + 31));
        pr_info("eeprom[32]:  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
                *(ucp + 32), *(ucp + 33), *(ucp + 34), *(ucp + 35), *(ucp + 36), *(ucp + 37), *(ucp + 38), *(ucp + 39));
        pr_info("eeprom[40]:  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
                *(ucp + 40), *(ucp + 41), *(ucp + 42), *(ucp + 43), *(ucp + 44), *(ucp + 45), *(ucp + 46), *(ucp + 47));
    }
#endif
#if 0
    pr_info("sn: %x %x %x %x %x %x\n",
            hi->mfg_info.Serial[0],
            hi->mfg_info.Serial[1],
            hi->mfg_info.Serial[2],
            hi->mfg_info.Serial[3],
            hi->mfg_info.Serial[4],
            hi->mfg_info.Serial[5]);
#endif

    if ((hi->promfmt = pmc_verify_cksum (&hi->mfg_info.data)) == PROM_FORMAT_Unk)
    {
        /* bad crc, data is suspect */
        if (cxt1e1_log_level >= LOG_WARN)
            pr_info("%s: EEPROM cksum error\n", hi->devname);
        hi->mfg_info_sts = EEPROM_CRCERR;
    } else
        hi->mfg_info_sts = EEPROM_OK;
}


void        __init
prep_hdw_info (void)
{
    hdw_info_t *hi;
    int         i;

    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        hi->pci_busno = 0xff;
        hi->pci_slot = 0xff;
        hi->pci_pin[0] = 0;
        hi->pci_pin[1] = 0;
        hi->ndev = 0;
        hi->addr[0] = 0L;
        hi->addr[1] = 0L;
        hi->addr_mapped[0] = 0L;
        hi->addr_mapped[1] = 0L;
    }
}

void
cleanup_ioremap (void)
{
    hdw_info_t *hi;
    int         i;

    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        if (hi->pci_slot == 0xff)
            break;
        if (hi->addr_mapped[0])
        {
            iounmap ((void *) (hi->addr_mapped[0]));
            release_mem_region ((long) hi->addr[0], hi->len[0]);
            hi->addr_mapped[0] = 0;
        }
        if (hi->addr_mapped[1])
        {
            iounmap ((void *) (hi->addr_mapped[1]));
            release_mem_region ((long) hi->addr[1], hi->len[1]);
            hi->addr_mapped[1] = 0;
        }
    }
}


void
cleanup_devs (void)
{
    hdw_info_t *hi;
    int         i;

    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        if (hi->pci_slot == 0xff || !hi->ndev)
            break;
        c4_stopwd(netdev_priv(hi->ndev));
#ifdef CONFIG_PROC_FS
        sbecom_proc_brd_cleanup(netdev_priv(hi->ndev));
#endif
        unregister_netdev (hi->ndev);
        free_irq (hi->pdev[0]->irq, hi->ndev);
#ifdef CONFIG_SBE_PMCC4_NCOMM
        free_irq (hi->pdev[1]->irq, hi->ndev);
#endif
        OS_kfree (hi->ndev);
    }
}


STATIC int  __init
c4_hdw_init (struct pci_dev * pdev, int found)
{
    hdw_info_t *hi;
    int         i;
    int         fun, slot;
    unsigned char busno = 0xff;

    /* our MUSYCC chip supports two functions, 0 & 1 */
    if ((fun = PCI_FUNC (pdev->devfn)) > 1)
    {
        pr_warning("unexpected devfun: 0x%x\n", pdev->devfn);
        return 0;
    }
    if (pdev->bus)                  /* obtain bus number */
        busno = pdev->bus->number;
    else
        busno = 0;                  /* default for system PCI inconsistency */
    slot = pdev->devfn & ~0x07;

    /*
     * Functions 0 & 1 for a given board (identified by same bus(busno) and
     * slot(slot)) are placed into the same 'hardware' structure.  The first
     * part of the board's functionality will be placed into an unpopulated
     * element, identified by "slot==(0xff)".  The second part of a board's
     * functionality will match the previously loaded slot/busno.
     */
    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        /*
         * match with board's first found interface, otherwise this is first
         * found
         */
        if ((hi->pci_slot == 0xff) ||   /* new board */
            ((hi->pci_slot == slot) && (hi->bus == pdev->bus)))
            break;                  /* found for-loop exit */
    }
    if (i == MAX_BOARDS)            /* no match in above loop means MAX
                                     * exceeded */
    {
        pr_warning("exceeded number of allowed devices (>%d)?\n", MAX_BOARDS);
        return 0;
    }
    if (pdev->bus)
        hi->pci_busno = pdev->bus->number;
    else
        hi->pci_busno = 0;          /* default for system PCI inconsistency */
    hi->pci_slot = slot;
    pci_read_config_byte (pdev, PCI_INTERRUPT_PIN, &hi->pci_pin[fun]);
    pci_read_config_byte (pdev, PCI_REVISION_ID, &hi->revid[fun]);
    hi->bus = pdev->bus;
    hi->addr[fun] = pci_resource_start (pdev, 0);
    hi->len[fun] = pci_resource_end (pdev, 0) - hi->addr[fun] + 1;
    hi->pdev[fun] = pdev;

    {
        /*
         * create device name from module name, plus add the appropriate
         * board number
         */
        char       *cp = hi->devname;

        strcpy (cp, KBUILD_MODNAME);
        cp += strlen (cp);          /* reposition */
        *cp++ = '-';
        *cp++ = '0' + (found / 2);  /* there are two found interfaces per
                                     * board */
        *cp = 0;                    /* termination */
    }

    return 1;
}


status_t    __init
c4hw_attach_all (void)
{
    hdw_info_t *hi;
    struct pci_dev *pdev = NULL;
    int         found = 0, i, j;

    error_flag = 0;
    prep_hdw_info ();
    /*** scan PCI bus for all possible boards */
    while ((pdev = pci_get_device (PCI_VENDOR_ID_CONEXANT,
                                    PCI_DEVICE_ID_CN8474,
                                    pdev)))
    {
        if (c4_hdw_init (pdev, found))
            found++;
    }
    if (!found)
    {
        pr_warning("No boards found\n");
        return ENODEV;
    }
    /* sanity check for consistant hardware found */
    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        if (hi->pci_slot != 0xff && (!hi->addr[0] || !hi->addr[1]))
        {
            pr_warning("%s: something very wrong with pci_get_device\n",
                       hi->devname);
            return EIO;
        }
    }
    /* bring board's memory regions on/line */
    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        if (hi->pci_slot == 0xff)
            break;
        for (j = 0; j < 2; j++)
        {
            if (request_mem_region (hi->addr[j], hi->len[j], hi->devname) == 0)
            {
                pr_warning("%s: memory in use, addr=0x%lx, len=0x%lx ?\n",
                           hi->devname, hi->addr[j], hi->len[j]);
                cleanup_ioremap ();
                return ENOMEM;
            }
            hi->addr_mapped[j] = (unsigned long) ioremap (hi->addr[j], hi->len[j]);
            if (!hi->addr_mapped[j])
            {
                pr_warning("%s: ioremap fails, addr=0x%lx, len=0x%lx ?\n",
                           hi->devname, hi->addr[j], hi->len[j]);
                cleanup_ioremap ();
                return ENOMEM;
            }
#ifdef SBE_MAP_DEBUG
            pr_warning("%s: io remapped from phys %x to virt %x\n",
                       hi->devname, (u_int32_t) hi->addr[j], (u_int32_t) hi->addr_mapped[j]);
#endif
        }
    }

    drvr_state = SBE_DRVR_AVAILABLE;

    /* Have now memory mapped all boards.  Now allow board's access to system */
    for (i = 0, hi = hdw_info; i < MAX_BOARDS; i++, hi++)
    {
        if (hi->pci_slot == 0xff)
            break;
        if (pci_enable_device (hi->pdev[0]) ||
            pci_enable_device (hi->pdev[1]))
        {
            drvr_state = SBE_DRVR_DOWN;
            pr_warning("%s: failed to enable card %d slot %d\n",
                       hi->devname, i, hi->pci_slot);
            cleanup_devs ();
            cleanup_ioremap ();
            return EIO;
        }
        pci_set_master (hi->pdev[0]);
        pci_set_master (hi->pdev[1]);
        if (!(hi->ndev = c4_add_dev (hi, i, (long) hi->addr_mapped[0],
                                     (long) hi->addr_mapped[1],
                                     hi->pdev[0]->irq,
                                     hi->pdev[1]->irq)))
        {
            drvr_state = SBE_DRVR_DOWN;
            cleanup_ioremap ();
            /* NOTE: c4_add_dev() does its own device cleanup */
#if 0
            cleanup_devs ();
#endif
            return error_flag;      /* error_flag set w/in add_dev() */
        }
        show_two (hi, i);           /* displays found information */
    }
    return 0;
}

/***  End-of-File  ***/
