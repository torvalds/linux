/*
 * pcilynx.c - Texas Instruments PCILynx driver
 * Copyright (C) 1999,2000 Andreas Bombe <andreas.bombe@munich.netsurf.de>,
 *                         Stephan Linz <linz@mazet.de>
 *                         Manfred Weihs <weihs@ict.tuwien.ac.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * Contributions:
 *
 * Manfred Weihs <weihs@ict.tuwien.ac.at>
 *        reading bus info block (containing GUID) from serial
 *            eeprom via i2c and storing it in config ROM
 *        Reworked code for initiating bus resets
 *            (long, short, with or without hold-off)
 *        Enhancements in async and iso send code
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kdev_t.h>
#include <linux/dma-mapping.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

#include "csr1212.h"
#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "pcilynx.h"

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) printk(level "pcilynx: " fmt "\n" , ## args)
/* print card specific information */
#define PRINT(level, card, fmt, args...) printk(level "pcilynx%d: " fmt "\n" , card , ## args)

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
#define PRINT_GD(level, fmt, args...) printk(level "pcilynx: " fmt "\n" , ## args)
#define PRINTD(level, card, fmt, args...) printk(level "pcilynx%d: " fmt "\n" , card , ## args)
#else
#define PRINT_GD(level, fmt, args...) do {} while (0)
#define PRINTD(level, card, fmt, args...) do {} while (0)
#endif


/* Module Parameters */
static int skip_eeprom;
module_param(skip_eeprom, int, 0444);
MODULE_PARM_DESC(skip_eeprom, "Use generic bus info block instead of serial eeprom (default = 0).");


static struct hpsb_host_driver lynx_driver;
static unsigned int card_id;



/*
 * I2C stuff
 */

/* the i2c stuff was inspired by i2c-philips-par.c */

static void bit_setscl(void *data, int state)
{
	if (state) {
		  ((struct ti_lynx *) data)->i2c_driven_state |= 0x00000040;
	} else {
		  ((struct ti_lynx *) data)->i2c_driven_state &= ~0x00000040;
	}
	reg_write((struct ti_lynx *) data, SERIAL_EEPROM_CONTROL, ((struct ti_lynx *) data)->i2c_driven_state);
}

static void bit_setsda(void *data, int state)
{
	if (state) {
		  ((struct ti_lynx *) data)->i2c_driven_state |= 0x00000010;
	} else {
		  ((struct ti_lynx *) data)->i2c_driven_state &= ~0x00000010;
	}
	reg_write((struct ti_lynx *) data, SERIAL_EEPROM_CONTROL, ((struct ti_lynx *) data)->i2c_driven_state);
}

static int bit_getscl(void *data)
{
	return reg_read((struct ti_lynx *) data, SERIAL_EEPROM_CONTROL) & 0x00000040;
}

static int bit_getsda(void *data)
{
	return reg_read((struct ti_lynx *) data, SERIAL_EEPROM_CONTROL) & 0x00000010;
}

static struct i2c_algo_bit_data bit_data = {
	.setsda			= bit_setsda,
	.setscl			= bit_setscl,
	.getsda			= bit_getsda,
	.getscl			= bit_getscl,
	.udelay			= 5,
	.timeout		= 100,
};


/*
 * PCL handling functions.
 */

static pcl_t alloc_pcl(struct ti_lynx *lynx)
{
        u8 m;
        int i, j;

        spin_lock(&lynx->lock);
        /* FIXME - use ffz() to make this readable */
        for (i = 0; i < (LOCALRAM_SIZE / 1024); i++) {
                m = lynx->pcl_bmap[i];
                for (j = 0; j < 8; j++) {
                        if (m & 1<<j) {
                                continue;
                        }
                        m |= 1<<j;
                        lynx->pcl_bmap[i] = m;
                        spin_unlock(&lynx->lock);
                        return 8 * i + j;
                }
        }
        spin_unlock(&lynx->lock);

        return -1;
}


#if 0
static void free_pcl(struct ti_lynx *lynx, pcl_t pclid)
{
        int off, bit;

        off = pclid / 8;
        bit = pclid % 8;

        if (pclid < 0) {
                return;
        }

        spin_lock(&lynx->lock);
        if (lynx->pcl_bmap[off] & 1<<bit) {
                lynx->pcl_bmap[off] &= ~(1<<bit);
        } else {
                PRINT(KERN_ERR, lynx->id,
                      "attempted to free unallocated PCL %d", pclid);
        }
        spin_unlock(&lynx->lock);
}

/* functions useful for debugging */
static void pretty_print_pcl(const struct ti_pcl *pcl)
{
        int i;

        printk("PCL next %08x, userdata %08x, status %08x, remtrans %08x, nextbuf %08x\n",
               pcl->next, pcl->user_data, pcl->pcl_status,
               pcl->remaining_transfer_count, pcl->next_data_buffer);

        printk("PCL");
        for (i=0; i<13; i++) {
                printk(" c%x:%08x d%x:%08x",
                       i, pcl->buffer[i].control, i, pcl->buffer[i].pointer);
                if (!(i & 0x3) && (i != 12)) printk("\nPCL");
        }
        printk("\n");
}

static void print_pcl(const struct ti_lynx *lynx, pcl_t pclid)
{
        struct ti_pcl pcl;

        get_pcl(lynx, pclid, &pcl);
        pretty_print_pcl(&pcl);
}
#endif



/***********************************
 * IEEE-1394 functionality section *
 ***********************************/


static int get_phy_reg(struct ti_lynx *lynx, int addr)
{
        int retval;
        int i = 0;

        unsigned long flags;

        if (addr > 15) {
                PRINT(KERN_ERR, lynx->id,
                      "%s: PHY register address %d out of range",
		      __func__, addr);
                return -1;
        }

        spin_lock_irqsave(&lynx->phy_reg_lock, flags);

        reg_write(lynx, LINK_PHY, LINK_PHY_READ | LINK_PHY_ADDR(addr));
        do {
                retval = reg_read(lynx, LINK_PHY);

                if (i > 10000) {
                        PRINT(KERN_ERR, lynx->id, "%s: runaway loop, aborting",
			      __func__);
                        retval = -1;
                        break;
                }
                i++;
        } while ((retval & 0xf00) != LINK_PHY_RADDR(addr));

        reg_write(lynx, LINK_INT_STATUS, LINK_INT_PHY_REG_RCVD);
        spin_unlock_irqrestore(&lynx->phy_reg_lock, flags);

        if (retval != -1) {
                return retval & 0xff;
        } else {
                return -1;
        }
}

static int set_phy_reg(struct ti_lynx *lynx, int addr, int val)
{
        unsigned long flags;

        if (addr > 15) {
                PRINT(KERN_ERR, lynx->id,
		      "%s: PHY register address %d out of range", __func__, addr);
                return -1;
        }

        if (val > 0xff) {
                PRINT(KERN_ERR, lynx->id,
		      "%s: PHY register value %d out of range", __func__, val);
                return -1;
        }

        spin_lock_irqsave(&lynx->phy_reg_lock, flags);

        reg_write(lynx, LINK_PHY, LINK_PHY_WRITE | LINK_PHY_ADDR(addr)
                  | LINK_PHY_WDATA(val));

        spin_unlock_irqrestore(&lynx->phy_reg_lock, flags);

        return 0;
}

static int sel_phy_reg_page(struct ti_lynx *lynx, int page)
{
        int reg;

        if (page > 7) {
                PRINT(KERN_ERR, lynx->id,
		      "%s: PHY page %d out of range", __func__, page);
                return -1;
        }

        reg = get_phy_reg(lynx, 7);
        if (reg != -1) {
                reg &= 0x1f;
                reg |= (page << 5);
                set_phy_reg(lynx, 7, reg);
                return 0;
        } else {
                return -1;
        }
}

#if 0 /* not needed at this time */
static int sel_phy_reg_port(struct ti_lynx *lynx, int port)
{
        int reg;

        if (port > 15) {
                PRINT(KERN_ERR, lynx->id,
		      "%s: PHY port %d out of range", __func__, port);
                return -1;
        }

        reg = get_phy_reg(lynx, 7);
        if (reg != -1) {
                reg &= 0xf0;
                reg |= port;
                set_phy_reg(lynx, 7, reg);
                return 0;
        } else {
                return -1;
        }
}
#endif

static u32 get_phy_vendorid(struct ti_lynx *lynx)
{
        u32 pvid = 0;
        sel_phy_reg_page(lynx, 1);
        pvid |= (get_phy_reg(lynx, 10) << 16);
        pvid |= (get_phy_reg(lynx, 11) << 8);
        pvid |= get_phy_reg(lynx, 12);
        PRINT(KERN_INFO, lynx->id, "PHY vendor id 0x%06x", pvid);
        return pvid;
}

static u32 get_phy_productid(struct ti_lynx *lynx)
{
        u32 id = 0;
        sel_phy_reg_page(lynx, 1);
        id |= (get_phy_reg(lynx, 13) << 16);
        id |= (get_phy_reg(lynx, 14) << 8);
        id |= get_phy_reg(lynx, 15);
        PRINT(KERN_INFO, lynx->id, "PHY product id 0x%06x", id);
        return id;
}

static quadlet_t generate_own_selfid(struct ti_lynx *lynx,
                                     struct hpsb_host *host)
{
        quadlet_t lsid;
        char phyreg[7];
        int i;

        phyreg[0] = lynx->phy_reg0;
        for (i = 1; i < 7; i++) {
                phyreg[i] = get_phy_reg(lynx, i);
        }

        /* FIXME? We assume a TSB21LV03A phy here.  This code doesn't support
           more than 3 ports on the PHY anyway. */

        lsid = 0x80400000 | ((phyreg[0] & 0xfc) << 22);
        lsid |= (phyreg[1] & 0x3f) << 16; /* gap count */
        lsid |= (phyreg[2] & 0xc0) << 8; /* max speed */
	if (!hpsb_disable_irm)
		lsid |= (phyreg[6] & 0x01) << 11; /* contender (phy dependent) */
        /* lsid |= 1 << 11; *//* set contender (hack) */
        lsid |= (phyreg[6] & 0x10) >> 3; /* initiated reset */

        for (i = 0; i < (phyreg[2] & 0xf); i++) { /* ports */
                if (phyreg[3 + i] & 0x4) {
                        lsid |= (((phyreg[3 + i] & 0x8) | 0x10) >> 3)
                                << (6 - i*2);
                } else {
                        lsid |= 1 << (6 - i*2);
                }
        }

        cpu_to_be32s(&lsid);
        PRINT(KERN_DEBUG, lynx->id, "generated own selfid 0x%x", lsid);
        return lsid;
}

static void handle_selfid(struct ti_lynx *lynx, struct hpsb_host *host)
{
        quadlet_t *q = lynx->rcv_page;
        int phyid, isroot, size;
        quadlet_t lsid = 0;
        int i;

        if (lynx->phy_reg0 == -1 || lynx->selfid_size == -1) return;

        size = lynx->selfid_size;
        phyid = lynx->phy_reg0;

        i = (size > 16 ? 16 : size) / 4 - 1;
        while (i >= 0) {
                cpu_to_be32s(&q[i]);
                i--;
        }

        if (!lynx->phyic.reg_1394a) {
                lsid = generate_own_selfid(lynx, host);
        }

        isroot = (phyid & 2) != 0;
        phyid >>= 2;
        PRINT(KERN_INFO, lynx->id, "SelfID process finished (phyid %d, %s)",
              phyid, (isroot ? "root" : "not root"));
        reg_write(lynx, LINK_ID, (0xffc0 | phyid) << 16);

        if (!lynx->phyic.reg_1394a && !size) {
                hpsb_selfid_received(host, lsid);
        }

        while (size > 0) {
                struct selfid *sid = (struct selfid *)q;

                if (!lynx->phyic.reg_1394a && !sid->extended
                    && (sid->phy_id == (phyid + 1))) {
                        hpsb_selfid_received(host, lsid);
                }

                if (q[0] == ~q[1]) {
                        PRINT(KERN_DEBUG, lynx->id, "SelfID packet 0x%x rcvd",
                              q[0]);
                        hpsb_selfid_received(host, q[0]);
                } else {
                        PRINT(KERN_INFO, lynx->id,
                              "inconsistent selfid 0x%x/0x%x", q[0], q[1]);
                }
                q += 2;
                size -= 8;
        }

        if (!lynx->phyic.reg_1394a && isroot && phyid != 0) {
                hpsb_selfid_received(host, lsid);
        }

        hpsb_selfid_complete(host, phyid, isroot);

        if (host->in_bus_reset) return; /* in bus reset again */

        if (isroot) reg_set_bits(lynx, LINK_CONTROL, LINK_CONTROL_CYCMASTER); //FIXME: I do not think, we need this here
        reg_set_bits(lynx, LINK_CONTROL,
                     LINK_CONTROL_RCV_CMP_VALID | LINK_CONTROL_TX_ASYNC_EN
                     | LINK_CONTROL_RX_ASYNC_EN | LINK_CONTROL_CYCTIMEREN);
}



/* This must be called with the respective queue_lock held. */
static void send_next(struct ti_lynx *lynx, int what)
{
        struct ti_pcl pcl;
        struct lynx_send_data *d;
        struct hpsb_packet *packet;

#if 0 /* has been removed from ieee1394 core */
        d = (what == hpsb_iso ? &lynx->iso_send : &lynx->async);
#else
	d = &lynx->async;
#endif
        if (!list_empty(&d->pcl_queue)) {
                PRINT(KERN_ERR, lynx->id, "trying to queue a new packet in nonempty fifo");
                BUG();
        }

        packet = driver_packet(d->queue.next);
	list_move_tail(&packet->driver_list, &d->pcl_queue);

        d->header_dma = pci_map_single(lynx->dev, packet->header,
                                       packet->header_size, PCI_DMA_TODEVICE);
        if (packet->data_size) {
                d->data_dma = pci_map_single(lynx->dev, packet->data,
                                             packet->data_size,
                                             PCI_DMA_TODEVICE);
        } else {
                d->data_dma = 0;
        }

        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
        pcl.pcl_status = 0;
        pcl.buffer[0].control = packet->speed_code << 14 | packet->header_size;
#ifndef __BIG_ENDIAN
        pcl.buffer[0].control |= PCL_BIGENDIAN;
#endif
        pcl.buffer[0].pointer = d->header_dma;
        pcl.buffer[1].control = PCL_LAST_BUFF | packet->data_size;
        pcl.buffer[1].pointer = d->data_dma;

        switch (packet->type) {
        case hpsb_async:
                pcl.buffer[0].control |= PCL_CMD_XMT;
                break;
#if 0 /* has been removed from ieee1394 core */
        case hpsb_iso:
                pcl.buffer[0].control |= PCL_CMD_XMT | PCL_ISOMODE;
                break;
#endif
        case hpsb_raw:
                pcl.buffer[0].control |= PCL_CMD_UNFXMT;
                break;
        }

        put_pcl(lynx, d->pcl, &pcl);
        run_pcl(lynx, d->pcl_start, d->channel);
}


/* called from subsystem core */
static int lynx_transmit(struct hpsb_host *host, struct hpsb_packet *packet)
{
        struct ti_lynx *lynx = host->hostdata;
        struct lynx_send_data *d;
        unsigned long flags;

        if (packet->data_size >= 4096) {
                PRINT(KERN_ERR, lynx->id, "transmit packet data too big (%Zd)",
                      packet->data_size);
                return -EOVERFLOW;
        }

        switch (packet->type) {
        case hpsb_async:
        case hpsb_raw:
                d = &lynx->async;
                break;
#if 0 /* has been removed from ieee1394 core */
        case hpsb_iso:
                d = &lynx->iso_send;
                break;
#endif
        default:
                PRINT(KERN_ERR, lynx->id, "invalid packet type %d",
                      packet->type);
                return -EINVAL;
        }

        if (packet->tcode == TCODE_WRITEQ
            || packet->tcode == TCODE_READQ_RESPONSE) {
                cpu_to_be32s(&packet->header[3]);
        }

        spin_lock_irqsave(&d->queue_lock, flags);

	list_add_tail(&packet->driver_list, &d->queue);
	if (list_empty(&d->pcl_queue))
                send_next(lynx, packet->type);

        spin_unlock_irqrestore(&d->queue_lock, flags);

        return 0;
}


/* called from subsystem core */
static int lynx_devctl(struct hpsb_host *host, enum devctl_cmd cmd, int arg)
{
        struct ti_lynx *lynx = host->hostdata;
        int retval = 0;
        struct hpsb_packet *packet;
	LIST_HEAD(packet_list);
        unsigned long flags;
	int phy_reg;

        switch (cmd) {
        case RESET_BUS:
                if (reg_read(lynx, LINK_INT_STATUS) & LINK_INT_PHY_BUSRESET) {
                        retval = 0;
                        break;
                }

		switch (arg) {
		case SHORT_RESET:
			if (lynx->phyic.reg_1394a) {
				phy_reg = get_phy_reg(lynx, 5);
				if (phy_reg == -1) {
					PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
					retval = -1;
					break;
				}
				phy_reg |= 0x40;

				PRINT(KERN_INFO, lynx->id, "resetting bus (short bus reset) on request");

				lynx->selfid_size = -1;
				lynx->phy_reg0 = -1;
				set_phy_reg(lynx, 5, phy_reg); /* set ISBR */
				break;
			} else {
				PRINT(KERN_INFO, lynx->id, "cannot do short bus reset, because of old phy");
				/* fall through to long bus reset */
			}
		case LONG_RESET:
			phy_reg = get_phy_reg(lynx, 1);
			if (phy_reg == -1) {
				PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
				retval = -1;
				break;
			}
			phy_reg |= 0x40;

			PRINT(KERN_INFO, lynx->id, "resetting bus (long bus reset) on request");

			lynx->selfid_size = -1;
			lynx->phy_reg0 = -1;
			set_phy_reg(lynx, 1, phy_reg); /* clear RHB, set IBR */
			break;
		case SHORT_RESET_NO_FORCE_ROOT:
			if (lynx->phyic.reg_1394a) {
				phy_reg = get_phy_reg(lynx, 1);
				if (phy_reg == -1) {
					PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
					retval = -1;
					break;
				}
				if (phy_reg & 0x80) {
					phy_reg &= ~0x80;
					set_phy_reg(lynx, 1, phy_reg); /* clear RHB */
				}

				phy_reg = get_phy_reg(lynx, 5);
				if (phy_reg == -1) {
					PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
					retval = -1;
					break;
				}
				phy_reg |= 0x40;

				PRINT(KERN_INFO, lynx->id, "resetting bus (short bus reset, no force_root) on request");

				lynx->selfid_size = -1;
				lynx->phy_reg0 = -1;
				set_phy_reg(lynx, 5, phy_reg); /* set ISBR */
				break;
			} else {
				PRINT(KERN_INFO, lynx->id, "cannot do short bus reset, because of old phy");
				/* fall through to long bus reset */
			}
		case LONG_RESET_NO_FORCE_ROOT:
			phy_reg = get_phy_reg(lynx, 1);
			if (phy_reg == -1) {
				PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
				retval = -1;
				break;
			}
			phy_reg &= ~0x80;
			phy_reg |= 0x40;

			PRINT(KERN_INFO, lynx->id, "resetting bus (long bus reset, no force_root) on request");

			lynx->selfid_size = -1;
			lynx->phy_reg0 = -1;
			set_phy_reg(lynx, 1, phy_reg); /* clear RHB, set IBR */
			break;
		case SHORT_RESET_FORCE_ROOT:
			if (lynx->phyic.reg_1394a) {
				phy_reg = get_phy_reg(lynx, 1);
				if (phy_reg == -1) {
					PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
					retval = -1;
					break;
				}
				if (!(phy_reg & 0x80)) {
					phy_reg |= 0x80;
					set_phy_reg(lynx, 1, phy_reg); /* set RHB */
				}

				phy_reg = get_phy_reg(lynx, 5);
				if (phy_reg == -1) {
					PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
					retval = -1;
					break;
				}
				phy_reg |= 0x40;

				PRINT(KERN_INFO, lynx->id, "resetting bus (short bus reset, force_root set) on request");

				lynx->selfid_size = -1;
				lynx->phy_reg0 = -1;
				set_phy_reg(lynx, 5, phy_reg); /* set ISBR */
				break;
			} else {
				PRINT(KERN_INFO, lynx->id, "cannot do short bus reset, because of old phy");
				/* fall through to long bus reset */
			}
		case LONG_RESET_FORCE_ROOT:
			phy_reg = get_phy_reg(lynx, 1);
			if (phy_reg == -1) {
				PRINT(KERN_ERR, lynx->id, "cannot reset bus, because read phy reg failed");
				retval = -1;
				break;
			}
			phy_reg |= 0xc0;

			PRINT(KERN_INFO, lynx->id, "resetting bus (long bus reset, force_root set) on request");

			lynx->selfid_size = -1;
			lynx->phy_reg0 = -1;
			set_phy_reg(lynx, 1, phy_reg); /* set IBR and RHB */
			break;
		default:
			PRINT(KERN_ERR, lynx->id, "unknown argument for reset_bus command %d", arg);
			retval = -1;
		}

                break;

        case GET_CYCLE_COUNTER:
                retval = reg_read(lynx, CYCLE_TIMER);
                break;

        case SET_CYCLE_COUNTER:
                reg_write(lynx, CYCLE_TIMER, arg);
                break;

        case SET_BUS_ID:
                reg_write(lynx, LINK_ID,
                          (arg << 22) | (reg_read(lynx, LINK_ID) & 0x003f0000));
                break;

        case ACT_CYCLE_MASTER:
                if (arg) {
                        reg_set_bits(lynx, LINK_CONTROL,
                                     LINK_CONTROL_CYCMASTER);
                } else {
                        reg_clear_bits(lynx, LINK_CONTROL,
                                       LINK_CONTROL_CYCMASTER);
                }
                break;

        case CANCEL_REQUESTS:
                spin_lock_irqsave(&lynx->async.queue_lock, flags);

                reg_write(lynx, DMA_CHAN_CTRL(CHANNEL_ASYNC_SEND), 0);
		list_splice_init(&lynx->async.queue, &packet_list);

                if (list_empty(&lynx->async.pcl_queue)) {
                        spin_unlock_irqrestore(&lynx->async.queue_lock, flags);
                        PRINTD(KERN_DEBUG, lynx->id, "no async packet in PCL to cancel");
                } else {
                        struct ti_pcl pcl;
                        u32 ack;

                        PRINT(KERN_INFO, lynx->id, "cancelling async packet, that was already in PCL");

                        get_pcl(lynx, lynx->async.pcl, &pcl);

                        packet = driver_packet(lynx->async.pcl_queue.next);
			list_del_init(&packet->driver_list);

                        pci_unmap_single(lynx->dev, lynx->async.header_dma,
                                         packet->header_size, PCI_DMA_TODEVICE);
                        if (packet->data_size) {
                                pci_unmap_single(lynx->dev, lynx->async.data_dma,
                                                 packet->data_size, PCI_DMA_TODEVICE);
                        }

                        spin_unlock_irqrestore(&lynx->async.queue_lock, flags);

                        if (pcl.pcl_status & DMA_CHAN_STAT_PKTCMPL) {
                                if (pcl.pcl_status & DMA_CHAN_STAT_SPECIALACK) {
                                        ack = (pcl.pcl_status >> 15) & 0xf;
                                        PRINTD(KERN_INFO, lynx->id, "special ack %d", ack);
                                        ack = (ack == 1 ? ACKX_TIMEOUT : ACKX_SEND_ERROR);
                                } else {
                                        ack = (pcl.pcl_status >> 15) & 0xf;
                                }
                        } else {
                                PRINT(KERN_INFO, lynx->id, "async packet was not completed");
                                ack = ACKX_ABORTED;
                        }
                        hpsb_packet_sent(host, packet, ack);
                }

		while (!list_empty(&packet_list)) {
			packet = driver_packet(packet_list.next);
			list_del_init(&packet->driver_list);
			hpsb_packet_sent(host, packet, ACKX_ABORTED);
		}

                break;
#if 0 /* has been removed from ieee1394 core */
        case ISO_LISTEN_CHANNEL:
                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);

                if (lynx->iso_rcv.chan_count++ == 0) {
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                                  DMA_WORD1_CMP_ENABLE_MASTER);
                }

                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
                break;

        case ISO_UNLISTEN_CHANNEL:
                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);

                if (--lynx->iso_rcv.chan_count == 0) {
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                                  0);
                }

                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
                break;
#endif
        default:
                PRINT(KERN_ERR, lynx->id, "unknown devctl command %d", cmd);
                retval = -1;
        }

        return retval;
}


/***************************************
 * IEEE-1394 functionality section END *
 ***************************************/


/********************************************************
 * Global stuff (interrupt handler, init/shutdown code) *
 ********************************************************/


static irqreturn_t lynx_irq_handler(int irq, void *dev_id)
{
        struct ti_lynx *lynx = (struct ti_lynx *)dev_id;
        struct hpsb_host *host = lynx->host;
        u32 intmask;
        u32 linkint;

        linkint = reg_read(lynx, LINK_INT_STATUS);
        intmask = reg_read(lynx, PCI_INT_STATUS);

        if (!(intmask & PCI_INT_INT_PEND))
		return IRQ_NONE;

        PRINTD(KERN_DEBUG, lynx->id, "interrupt: 0x%08x / 0x%08x", intmask,
               linkint);

        reg_write(lynx, LINK_INT_STATUS, linkint);
        reg_write(lynx, PCI_INT_STATUS, intmask);

        if (intmask & PCI_INT_1394) {
                if (linkint & LINK_INT_PHY_TIMEOUT) {
                        PRINT(KERN_INFO, lynx->id, "PHY timeout occurred");
                }
                if (linkint & LINK_INT_PHY_BUSRESET) {
                        PRINT(KERN_INFO, lynx->id, "bus reset interrupt");
                        lynx->selfid_size = -1;
                        lynx->phy_reg0 = -1;
                        if (!host->in_bus_reset)
                                hpsb_bus_reset(host);
                }
                if (linkint & LINK_INT_PHY_REG_RCVD) {
                        u32 reg;

                        spin_lock(&lynx->phy_reg_lock);
                        reg = reg_read(lynx, LINK_PHY);
                        spin_unlock(&lynx->phy_reg_lock);

                        if (!host->in_bus_reset) {
                                PRINT(KERN_INFO, lynx->id,
                                      "phy reg received without reset");
                        } else if (reg & 0xf00) {
                                PRINT(KERN_INFO, lynx->id,
                                      "unsolicited phy reg %d received",
                                      (reg >> 8) & 0xf);
                        } else {
                                lynx->phy_reg0 = reg & 0xff;
                                handle_selfid(lynx, host);
                        }
                }
                if (linkint & LINK_INT_ISO_STUCK) {
                        PRINT(KERN_INFO, lynx->id, "isochronous transmitter stuck");
                }
                if (linkint & LINK_INT_ASYNC_STUCK) {
                        PRINT(KERN_INFO, lynx->id, "asynchronous transmitter stuck");
                }
                if (linkint & LINK_INT_SENT_REJECT) {
                        PRINT(KERN_INFO, lynx->id, "sent reject");
                }
                if (linkint & LINK_INT_TX_INVALID_TC) {
                        PRINT(KERN_INFO, lynx->id, "invalid transaction code");
                }
                if (linkint & LINK_INT_GRF_OVERFLOW) {
                        /* flush FIFO if overflow happens during reset */
                        if (host->in_bus_reset)
                                reg_write(lynx, FIFO_CONTROL,
                                          FIFO_CONTROL_GRF_FLUSH);
                        PRINT(KERN_INFO, lynx->id, "GRF overflow");
                }
                if (linkint & LINK_INT_ITF_UNDERFLOW) {
                        PRINT(KERN_INFO, lynx->id, "ITF underflow");
                }
                if (linkint & LINK_INT_ATF_UNDERFLOW) {
                        PRINT(KERN_INFO, lynx->id, "ATF underflow");
                }
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ISO_RCV)) {
                PRINTD(KERN_DEBUG, lynx->id, "iso receive");

                spin_lock(&lynx->iso_rcv.lock);

                lynx->iso_rcv.stat[lynx->iso_rcv.next] =
                        reg_read(lynx, DMA_CHAN_STAT(CHANNEL_ISO_RCV));

                lynx->iso_rcv.used++;
                lynx->iso_rcv.next = (lynx->iso_rcv.next + 1) % NUM_ISORCV_PCL;

                if ((lynx->iso_rcv.next == lynx->iso_rcv.last)
                    || !lynx->iso_rcv.chan_count) {
                        PRINTD(KERN_DEBUG, lynx->id, "stopped");
                        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV), 0);
                }

                run_sub_pcl(lynx, lynx->iso_rcv.pcl_start, lynx->iso_rcv.next,
                            CHANNEL_ISO_RCV);

                spin_unlock(&lynx->iso_rcv.lock);

		tasklet_schedule(&lynx->iso_rcv.tq);
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ASYNC_SEND)) {
                PRINTD(KERN_DEBUG, lynx->id, "async sent");
                spin_lock(&lynx->async.queue_lock);

                if (list_empty(&lynx->async.pcl_queue)) {
                        spin_unlock(&lynx->async.queue_lock);
                        PRINT(KERN_WARNING, lynx->id, "async dma halted, but no queued packet (maybe it was cancelled)");
                } else {
                        struct ti_pcl pcl;
                        u32 ack;
                        struct hpsb_packet *packet;

                        get_pcl(lynx, lynx->async.pcl, &pcl);

                        packet = driver_packet(lynx->async.pcl_queue.next);
                        list_del_init(&packet->driver_list);

                        pci_unmap_single(lynx->dev, lynx->async.header_dma,
                                         packet->header_size, PCI_DMA_TODEVICE);
                        if (packet->data_size) {
                                pci_unmap_single(lynx->dev, lynx->async.data_dma,
                                                 packet->data_size, PCI_DMA_TODEVICE);
                        }

                        if (!list_empty(&lynx->async.queue)) {
                                send_next(lynx, hpsb_async);
                        }

                        spin_unlock(&lynx->async.queue_lock);

                        if (pcl.pcl_status & DMA_CHAN_STAT_PKTCMPL) {
                                if (pcl.pcl_status & DMA_CHAN_STAT_SPECIALACK) {
                                        ack = (pcl.pcl_status >> 15) & 0xf;
                                        PRINTD(KERN_INFO, lynx->id, "special ack %d", ack);
                                        ack = (ack == 1 ? ACKX_TIMEOUT : ACKX_SEND_ERROR);
                                } else {
                                        ack = (pcl.pcl_status >> 15) & 0xf;
                                }
                        } else {
                                PRINT(KERN_INFO, lynx->id, "async packet was not completed");
                                ack = ACKX_SEND_ERROR;
                        }
                        hpsb_packet_sent(host, packet, ack);
                }
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ISO_SEND)) {
                PRINTD(KERN_DEBUG, lynx->id, "iso sent");
                spin_lock(&lynx->iso_send.queue_lock);

                if (list_empty(&lynx->iso_send.pcl_queue)) {
                        spin_unlock(&lynx->iso_send.queue_lock);
                        PRINT(KERN_ERR, lynx->id, "iso send dma halted, but no queued packet");
                } else {
                        struct ti_pcl pcl;
                        u32 ack;
                        struct hpsb_packet *packet;

                        get_pcl(lynx, lynx->iso_send.pcl, &pcl);

                        packet = driver_packet(lynx->iso_send.pcl_queue.next);
                        list_del_init(&packet->driver_list);

                        pci_unmap_single(lynx->dev, lynx->iso_send.header_dma,
                                         packet->header_size, PCI_DMA_TODEVICE);
                        if (packet->data_size) {
                                pci_unmap_single(lynx->dev, lynx->iso_send.data_dma,
                                                 packet->data_size, PCI_DMA_TODEVICE);
                        }
#if 0 /* has been removed from ieee1394 core */
                        if (!list_empty(&lynx->iso_send.queue)) {
                                send_next(lynx, hpsb_iso);
                        }
#endif
                        spin_unlock(&lynx->iso_send.queue_lock);

                        if (pcl.pcl_status & DMA_CHAN_STAT_PKTCMPL) {
                                if (pcl.pcl_status & DMA_CHAN_STAT_SPECIALACK) {
                                        ack = (pcl.pcl_status >> 15) & 0xf;
                                        PRINTD(KERN_INFO, lynx->id, "special ack %d", ack);
                                        ack = (ack == 1 ? ACKX_TIMEOUT : ACKX_SEND_ERROR);
                                } else {
                                        ack = (pcl.pcl_status >> 15) & 0xf;
                                }
                        } else {
                                PRINT(KERN_INFO, lynx->id, "iso send packet was not completed");
                                ack = ACKX_SEND_ERROR;
                        }

                        hpsb_packet_sent(host, packet, ack); //FIXME: maybe we should just use ACK_COMPLETE and ACKX_SEND_ERROR
                }
        }

        if (intmask & PCI_INT_DMA_HLT(CHANNEL_ASYNC_RCV)) {
                /* general receive DMA completed */
                int stat = reg_read(lynx, DMA_CHAN_STAT(CHANNEL_ASYNC_RCV));

                PRINTD(KERN_DEBUG, lynx->id, "received packet size %d",
                       stat & 0x1fff);

                if (stat & DMA_CHAN_STAT_SELFID) {
                        lynx->selfid_size = stat & 0x1fff;
                        handle_selfid(lynx, host);
                } else {
                        quadlet_t *q_data = lynx->rcv_page;
                        if ((*q_data >> 4 & 0xf) == TCODE_READQ_RESPONSE
                            || (*q_data >> 4 & 0xf) == TCODE_WRITEQ) {
                                cpu_to_be32s(q_data + 3);
                        }
                        hpsb_packet_received(host, q_data, stat & 0x1fff, 0);
                }

                run_pcl(lynx, lynx->rcv_pcl_start, CHANNEL_ASYNC_RCV);
        }

	return IRQ_HANDLED;
}


static void iso_rcv_bh(struct ti_lynx *lynx)
{
        unsigned int idx;
        quadlet_t *data;
        unsigned long flags;

        spin_lock_irqsave(&lynx->iso_rcv.lock, flags);

        while (lynx->iso_rcv.used) {
                idx = lynx->iso_rcv.last;
                spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);

                data = lynx->iso_rcv.page[idx / ISORCV_PER_PAGE]
                        + (idx % ISORCV_PER_PAGE) * MAX_ISORCV_SIZE;

                if ((*data >> 16) + 4 != (lynx->iso_rcv.stat[idx] & 0x1fff)) {
                        PRINT(KERN_ERR, lynx->id,
                              "iso length mismatch 0x%08x/0x%08x", *data,
                              lynx->iso_rcv.stat[idx]);
                }

                if (lynx->iso_rcv.stat[idx]
                    & (DMA_CHAN_STAT_PCIERR | DMA_CHAN_STAT_PKTERR)) {
                        PRINT(KERN_INFO, lynx->id,
                              "iso receive error on %d to 0x%p", idx, data);
                } else {
                        hpsb_packet_received(lynx->host, data,
                                             lynx->iso_rcv.stat[idx] & 0x1fff,
                                             0);
                }

                spin_lock_irqsave(&lynx->iso_rcv.lock, flags);
                lynx->iso_rcv.last = (idx + 1) % NUM_ISORCV_PCL;
                lynx->iso_rcv.used--;
        }

        if (lynx->iso_rcv.chan_count) {
                reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV),
                          DMA_WORD1_CMP_ENABLE_MASTER);
        }
        spin_unlock_irqrestore(&lynx->iso_rcv.lock, flags);
}


static void remove_card(struct pci_dev *dev)
{
        struct ti_lynx *lynx;
	struct device *lynx_dev;
        int i;

        lynx = pci_get_drvdata(dev);
        if (!lynx) return;
        pci_set_drvdata(dev, NULL);

	lynx_dev = get_device(&lynx->host->device);

        switch (lynx->state) {
        case is_host:
                reg_write(lynx, PCI_INT_ENABLE, 0);
                hpsb_remove_host(lynx->host);
        case have_intr:
                reg_write(lynx, PCI_INT_ENABLE, 0);
                free_irq(lynx->dev->irq, lynx);

		/* Disable IRM Contender and LCtrl */
		if (lynx->phyic.reg_1394a)
			set_phy_reg(lynx, 4, ~0xc0 & get_phy_reg(lynx, 4));

		/* Let all other nodes know to ignore us */
		lynx_devctl(lynx->host, RESET_BUS, LONG_RESET_NO_FORCE_ROOT);

        case have_iomappings:
                reg_set_bits(lynx, MISC_CONTROL, MISC_CONTROL_SWRESET);
                /* Fix buggy cards with autoboot pin not tied low: */
                reg_write(lynx, DMA0_CHAN_CTRL, 0);
                iounmap(lynx->registers);
                iounmap(lynx->local_rom);
                iounmap(lynx->local_ram);
                iounmap(lynx->aux_port);
        case have_1394_buffers:
                for (i = 0; i < ISORCV_PAGES; i++) {
                        if (lynx->iso_rcv.page[i]) {
                                pci_free_consistent(lynx->dev, PAGE_SIZE,
                                                    lynx->iso_rcv.page[i],
                                                    lynx->iso_rcv.page_dma[i]);
                        }
                }
                pci_free_consistent(lynx->dev, PAGE_SIZE, lynx->rcv_page,
                                    lynx->rcv_page_dma);
        case have_aux_buf:
        case have_pcl_mem:
                pci_free_consistent(lynx->dev, LOCALRAM_SIZE, lynx->pcl_mem,
                                    lynx->pcl_mem_dma);
        case clear:
                /* do nothing - already freed */
                ;
        }

	tasklet_kill(&lynx->iso_rcv.tq);

	if (lynx_dev)
		put_device(lynx_dev);
}


static int __devinit add_card(struct pci_dev *dev,
                              const struct pci_device_id *devid_is_unused)
{
#define FAIL(fmt, args...) do { \
        PRINT_G(KERN_ERR, fmt , ## args); \
        remove_card(dev); \
        return error; \
        } while (0)

	char irq_buf[16];
	struct hpsb_host *host;
        struct ti_lynx *lynx; /* shortcut to currently handled device */
        struct ti_pcl pcl;
        u32 *pcli;
        int i;
        int error;

        error = -ENXIO;

        if (pci_set_dma_mask(dev, DMA_32BIT_MASK))
                FAIL("DMA address limits not supported for PCILynx hardware");
        if (pci_enable_device(dev))
                FAIL("failed to enable PCILynx hardware");
        pci_set_master(dev);

        error = -ENOMEM;

	host = hpsb_alloc_host(&lynx_driver, sizeof(struct ti_lynx), &dev->dev);
        if (!host) FAIL("failed to allocate control structure memory");

        lynx = host->hostdata;
	lynx->id = card_id++;
        lynx->dev = dev;
        lynx->state = clear;
	lynx->host = host;
        host->pdev = dev;
        pci_set_drvdata(dev, lynx);

        spin_lock_init(&lynx->lock);
        spin_lock_init(&lynx->phy_reg_lock);

        lynx->pcl_mem = pci_alloc_consistent(dev, LOCALRAM_SIZE,
                                             &lynx->pcl_mem_dma);

        if (lynx->pcl_mem != NULL) {
                lynx->state = have_pcl_mem;
                PRINT(KERN_INFO, lynx->id,
                      "allocated PCL memory %d Bytes @ 0x%p", LOCALRAM_SIZE,
                      lynx->pcl_mem);
        } else {
                FAIL("failed to allocate PCL memory area");
        }

        lynx->rcv_page = pci_alloc_consistent(dev, PAGE_SIZE,
                                              &lynx->rcv_page_dma);
        if (lynx->rcv_page == NULL) {
                FAIL("failed to allocate receive buffer");
        }
        lynx->state = have_1394_buffers;

        for (i = 0; i < ISORCV_PAGES; i++) {
                lynx->iso_rcv.page[i] =
                        pci_alloc_consistent(dev, PAGE_SIZE,
                                             &lynx->iso_rcv.page_dma[i]);
                if (lynx->iso_rcv.page[i] == NULL) {
                        FAIL("failed to allocate iso receive buffers");
                }
        }

        lynx->registers = ioremap_nocache(pci_resource_start(dev,0),
                                          PCILYNX_MAX_REGISTER);
        lynx->local_ram = ioremap(pci_resource_start(dev,1), PCILYNX_MAX_MEMORY);
        lynx->aux_port  = ioremap(pci_resource_start(dev,2), PCILYNX_MAX_MEMORY);
        lynx->local_rom = ioremap(pci_resource_start(dev,PCI_ROM_RESOURCE),
                                  PCILYNX_MAX_MEMORY);
        lynx->state = have_iomappings;

        if (lynx->registers == NULL) {
                FAIL("failed to remap registers - card not accessible");
        }

        reg_set_bits(lynx, MISC_CONTROL, MISC_CONTROL_SWRESET);
        /* Fix buggy cards with autoboot pin not tied low: */
        reg_write(lynx, DMA0_CHAN_CTRL, 0);

	sprintf (irq_buf, "%d", dev->irq);

        if (!request_irq(dev->irq, lynx_irq_handler, IRQF_SHARED,
                         PCILYNX_DRIVER_NAME, lynx)) {
                PRINT(KERN_INFO, lynx->id, "allocated interrupt %s", irq_buf);
                lynx->state = have_intr;
        } else {
                FAIL("failed to allocate shared interrupt %s", irq_buf);
        }

        /* alloc_pcl return values are not checked, it is expected that the
         * provided PCL space is sufficient for the initial allocations */
        lynx->rcv_pcl = alloc_pcl(lynx);
        lynx->rcv_pcl_start = alloc_pcl(lynx);
        lynx->async.pcl = alloc_pcl(lynx);
        lynx->async.pcl_start = alloc_pcl(lynx);
        lynx->iso_send.pcl = alloc_pcl(lynx);
        lynx->iso_send.pcl_start = alloc_pcl(lynx);

        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                lynx->iso_rcv.pcl[i] = alloc_pcl(lynx);
        }
        lynx->iso_rcv.pcl_start = alloc_pcl(lynx);

        /* all allocations successful - simple init stuff follows */

        reg_write(lynx, PCI_INT_ENABLE, PCI_INT_DMA_ALL);

	tasklet_init(&lynx->iso_rcv.tq, (void (*)(unsigned long))iso_rcv_bh,
		     (unsigned long)lynx);

        spin_lock_init(&lynx->iso_rcv.lock);

        spin_lock_init(&lynx->async.queue_lock);
        lynx->async.channel = CHANNEL_ASYNC_SEND;
        spin_lock_init(&lynx->iso_send.queue_lock);
        lynx->iso_send.channel = CHANNEL_ISO_SEND;

        PRINT(KERN_INFO, lynx->id, "remapped memory spaces reg 0x%p, rom 0x%p, "
              "ram 0x%p, aux 0x%p", lynx->registers, lynx->local_rom,
              lynx->local_ram, lynx->aux_port);

        /* now, looking for PHY register set */
        if ((get_phy_reg(lynx, 2) & 0xe0) == 0xe0) {
                lynx->phyic.reg_1394a = 1;
                PRINT(KERN_INFO, lynx->id,
                      "found 1394a conform PHY (using extended register set)");
                lynx->phyic.vendor = get_phy_vendorid(lynx);
                lynx->phyic.product = get_phy_productid(lynx);
        } else {
                lynx->phyic.reg_1394a = 0;
                PRINT(KERN_INFO, lynx->id, "found old 1394 PHY");
        }

        lynx->selfid_size = -1;
        lynx->phy_reg0 = -1;

	INIT_LIST_HEAD(&lynx->async.queue);
	INIT_LIST_HEAD(&lynx->async.pcl_queue);
	INIT_LIST_HEAD(&lynx->iso_send.queue);
	INIT_LIST_HEAD(&lynx->iso_send.pcl_queue);

        pcl.next = pcl_bus(lynx, lynx->rcv_pcl);
        put_pcl(lynx, lynx->rcv_pcl_start, &pcl);

        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;

        pcl.buffer[0].control = PCL_CMD_RCV | 16;
#ifndef __BIG_ENDIAN
	pcl.buffer[0].control |= PCL_BIGENDIAN;
#endif
	pcl.buffer[1].control = PCL_LAST_BUFF | 4080;

        pcl.buffer[0].pointer = lynx->rcv_page_dma;
        pcl.buffer[1].pointer = lynx->rcv_page_dma + 16;
        put_pcl(lynx, lynx->rcv_pcl, &pcl);

        pcl.next = pcl_bus(lynx, lynx->async.pcl);
        pcl.async_error_next = pcl_bus(lynx, lynx->async.pcl);
        put_pcl(lynx, lynx->async.pcl_start, &pcl);

        pcl.next = pcl_bus(lynx, lynx->iso_send.pcl);
        pcl.async_error_next = PCL_NEXT_INVALID;
        put_pcl(lynx, lynx->iso_send.pcl_start, &pcl);

        pcl.next = PCL_NEXT_INVALID;
        pcl.async_error_next = PCL_NEXT_INVALID;
        pcl.buffer[0].control = PCL_CMD_RCV | 4;
#ifndef __BIG_ENDIAN
        pcl.buffer[0].control |= PCL_BIGENDIAN;
#endif
        pcl.buffer[1].control = PCL_LAST_BUFF | 2044;

        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                int page = i / ISORCV_PER_PAGE;
                int sec = i % ISORCV_PER_PAGE;

                pcl.buffer[0].pointer = lynx->iso_rcv.page_dma[page]
                        + sec * MAX_ISORCV_SIZE;
                pcl.buffer[1].pointer = pcl.buffer[0].pointer + 4;
                put_pcl(lynx, lynx->iso_rcv.pcl[i], &pcl);
        }

        pcli = (u32 *)&pcl;
        for (i = 0; i < NUM_ISORCV_PCL; i++) {
                pcli[i] = pcl_bus(lynx, lynx->iso_rcv.pcl[i]);
        }
        put_pcl(lynx, lynx->iso_rcv.pcl_start, &pcl);

        /* FIFO sizes from left to right: ITF=48 ATF=48 GRF=160 */
        reg_write(lynx, FIFO_SIZES, 0x003030a0);
        /* 20 byte threshold before triggering PCI transfer */
        reg_write(lynx, DMA_GLOBAL_REGISTER, 0x2<<24);
        /* threshold on both send FIFOs before transmitting:
           FIFO size - cache line size - 1 */
        i = reg_read(lynx, PCI_LATENCY_CACHELINE) & 0xff;
        i = 0x30 - i - 1;
        reg_write(lynx, FIFO_XMIT_THRESHOLD, (i << 8) | i);

        reg_set_bits(lynx, PCI_INT_ENABLE, PCI_INT_1394);

        reg_write(lynx, LINK_INT_ENABLE, LINK_INT_PHY_TIMEOUT
                  | LINK_INT_PHY_REG_RCVD  | LINK_INT_PHY_BUSRESET
                  | LINK_INT_ISO_STUCK     | LINK_INT_ASYNC_STUCK
                  | LINK_INT_SENT_REJECT   | LINK_INT_TX_INVALID_TC
                  | LINK_INT_GRF_OVERFLOW  | LINK_INT_ITF_UNDERFLOW
                  | LINK_INT_ATF_UNDERFLOW);

        reg_write(lynx, DMA_WORD0_CMP_VALUE(CHANNEL_ASYNC_RCV), 0);
        reg_write(lynx, DMA_WORD0_CMP_ENABLE(CHANNEL_ASYNC_RCV), 0xa<<4);
        reg_write(lynx, DMA_WORD1_CMP_VALUE(CHANNEL_ASYNC_RCV), 0);
        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ASYNC_RCV),
                  DMA_WORD1_CMP_MATCH_LOCAL_NODE | DMA_WORD1_CMP_MATCH_BROADCAST
                  | DMA_WORD1_CMP_MATCH_EXACT    | DMA_WORD1_CMP_MATCH_BUS_BCAST
                  | DMA_WORD1_CMP_ENABLE_SELF_ID | DMA_WORD1_CMP_ENABLE_MASTER);

        run_pcl(lynx, lynx->rcv_pcl_start, CHANNEL_ASYNC_RCV);

        reg_write(lynx, DMA_WORD0_CMP_VALUE(CHANNEL_ISO_RCV), 0);
        reg_write(lynx, DMA_WORD0_CMP_ENABLE(CHANNEL_ISO_RCV), 0x9<<4);
        reg_write(lynx, DMA_WORD1_CMP_VALUE(CHANNEL_ISO_RCV), 0);
        reg_write(lynx, DMA_WORD1_CMP_ENABLE(CHANNEL_ISO_RCV), 0);

        run_sub_pcl(lynx, lynx->iso_rcv.pcl_start, 0, CHANNEL_ISO_RCV);

        reg_write(lynx, LINK_CONTROL, LINK_CONTROL_RCV_CMP_VALID
                  | LINK_CONTROL_TX_ISO_EN   | LINK_CONTROL_RX_ISO_EN
                  | LINK_CONTROL_TX_ASYNC_EN | LINK_CONTROL_RX_ASYNC_EN
                  | LINK_CONTROL_RESET_TX    | LINK_CONTROL_RESET_RX);

	if (!lynx->phyic.reg_1394a) {
		if (!hpsb_disable_irm) {
			/* attempt to enable contender bit -FIXME- would this
			 * work elsewhere? */
			reg_set_bits(lynx, GPIO_CTRL_A, 0x1);
			reg_write(lynx, GPIO_DATA_BASE + 0x3c, 0x1);
		}
	} else {
		/* set the contender (if appropriate) and LCtrl bit in the
		 * extended PHY register set. (Should check that PHY_02_EXTENDED
		 * is set in register 2?)
		 */
		i = get_phy_reg(lynx, 4);
		i |= PHY_04_LCTRL;
		if (hpsb_disable_irm)
			i &= ~PHY_04_CONTENDER;
		else
			i |= PHY_04_CONTENDER;
		if (i != -1) set_phy_reg(lynx, 4, i);
	}
	
        if (!skip_eeprom)
        {
        	/* needed for i2c communication with serial eeprom */
        	struct i2c_adapter *i2c_ad;
        	struct i2c_algo_bit_data i2c_adapter_data;

        	error = -ENOMEM;
		i2c_ad = kzalloc(sizeof(*i2c_ad), GFP_KERNEL);
        	if (!i2c_ad) FAIL("failed to allocate I2C adapter memory");

		i2c_ad->id = I2C_HW_B_PCILYNX;
		strlcpy(i2c_ad->name, "PCILynx I2C", sizeof(i2c_ad->name));
                i2c_adapter_data = bit_data;
                i2c_ad->algo_data = &i2c_adapter_data;
                i2c_adapter_data.data = lynx;
		i2c_ad->dev.parent = &dev->dev;

		PRINTD(KERN_DEBUG, lynx->id,"original eeprom control: %d",
		       reg_read(lynx, SERIAL_EEPROM_CONTROL));

        	/* reset hardware to sane state */
        	lynx->i2c_driven_state = 0x00000070;
        	reg_write(lynx, SERIAL_EEPROM_CONTROL, lynx->i2c_driven_state);

        	if (i2c_bit_add_bus(i2c_ad) < 0)
        	{
			kfree(i2c_ad);
			error = -ENXIO;
			FAIL("unable to register i2c");
        	}
        	else
        	{
                        /* do i2c stuff */
                        unsigned char i2c_cmd = 0x10;
                        struct i2c_msg msg[2] = { { 0x50, 0, 1, &i2c_cmd },
                                                  { 0x50, I2C_M_RD, 20, (unsigned char*) lynx->bus_info_block }
                                                };

			/* we use i2c_transfer because we have no i2c_client
			   at hand */
                        if (i2c_transfer(i2c_ad, msg, 2) < 0) {
                                PRINT(KERN_ERR, lynx->id, "unable to read bus info block from i2c");
                        } else {
                                PRINT(KERN_INFO, lynx->id, "got bus info block from serial eeprom");
				/* FIXME: probably we shoud rewrite the max_rec, max_ROM(1394a),
				 * generation(1394a) and link_spd(1394a) field and recalculate
				 * the CRC */

                                for (i = 0; i < 5 ; i++)
                                        PRINTD(KERN_DEBUG, lynx->id, "Businfo block quadlet %i: %08x",
					       i, be32_to_cpu(lynx->bus_info_block[i]));

                                /* info_length, crc_length and 1394 magic number to check, if it is really a bus info block */
				if (((be32_to_cpu(lynx->bus_info_block[0]) & 0xffff0000) == 0x04040000) &&
				    (lynx->bus_info_block[1] == __constant_cpu_to_be32(0x31333934)))
                                {
                                        PRINT(KERN_DEBUG, lynx->id, "read a valid bus info block from");
                                } else {
					kfree(i2c_ad);
					error = -ENXIO;
					FAIL("read something from serial eeprom, but it does not seem to be a valid bus info block");
                                }

                        }

			i2c_del_adapter(i2c_ad);
			kfree(i2c_ad);
                }
        }

	host->csr.guid_hi = be32_to_cpu(lynx->bus_info_block[3]);
	host->csr.guid_lo = be32_to_cpu(lynx->bus_info_block[4]);
	host->csr.cyc_clk_acc = (be32_to_cpu(lynx->bus_info_block[2]) >> 16) & 0xff;
	host->csr.max_rec = (be32_to_cpu(lynx->bus_info_block[2]) >> 12) & 0xf;
	if (!lynx->phyic.reg_1394a)
		host->csr.lnk_spd = (get_phy_reg(lynx, 2) & 0xc0) >> 6;
	else
		host->csr.lnk_spd = be32_to_cpu(lynx->bus_info_block[2]) & 0x7;

	if (hpsb_add_host(host)) {
		error = -ENOMEM;
		FAIL("Failed to register host with highlevel");
	}

	lynx->state = is_host;

	return 0;
#undef FAIL
}


static struct pci_device_id pci_table[] = {
	{
                .vendor =    PCI_VENDOR_ID_TI,
                .device =    PCI_DEVICE_ID_TI_PCILYNX,
                .subvendor = PCI_ANY_ID,
                .subdevice = PCI_ANY_ID,
	},
	{ }			/* Terminating entry */
};

static struct pci_driver lynx_pci_driver = {
        .name =     PCILYNX_DRIVER_NAME,
        .id_table = pci_table,
        .probe =    add_card,
        .remove =   remove_card,
};

static struct hpsb_host_driver lynx_driver = {
	.owner =	   THIS_MODULE,
	.name =		   PCILYNX_DRIVER_NAME,
	.set_hw_config_rom = NULL,
        .transmit_packet = lynx_transmit,
        .devctl =          lynx_devctl,
	.isoctl =          NULL,
};

MODULE_AUTHOR("Andreas E. Bombe <andreas.bombe@munich.netsurf.de>");
MODULE_DESCRIPTION("driver for Texas Instruments PCI Lynx IEEE-1394 controller");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("pcilynx");
MODULE_DEVICE_TABLE(pci, pci_table);

static int __init pcilynx_init(void)
{
        int ret;

        ret = pci_register_driver(&lynx_pci_driver);
        if (ret < 0) {
                PRINT_G(KERN_ERR, "PCI module init failed");
                return ret;
        }

        return 0;
}

static void __exit pcilynx_cleanup(void)
{
        pci_unregister_driver(&lynx_pci_driver);
}


module_init(pcilynx_init);
module_exit(pcilynx_cleanup);
