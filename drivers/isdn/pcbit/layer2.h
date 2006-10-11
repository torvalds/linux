/*
 * PCBIT-D low-layer interface definitions
 *
 * Copyright (C) 1996 Universidade de Lisboa
 * 
 * Written by Pedro Roque Marques (roque@di.fc.ul.pt)
 *
 * This software may be used and distributed according to the terms of 
 * the GNU General Public License, incorporated herein by reference.
 */

/*
 * 19991203 - Fernando Carvalho - takion@superbofh.org
 * Hacked to compile with egcs and run with current version of isdn modules
*/

#ifndef LAYER2_H
#define LAYER2_H

#include <linux/interrupt.h>

#include <asm/byteorder.h>

#define BANK1 0x0000U /* PC -> Board */
#define BANK2 0x01ffU /* Board -> PC */
#define BANK3 0x03feU /* Att Board */
#define BANK4 0x03ffU /* Att PC */

#define BANKLEN 0x01FFU

#define LOAD_ZONE_START 0x03f8U
#define LOAD_ZONE_END   0x03fdU

#define LOAD_RETRY      18000000



/* TAM - XX - C - S  - NUM */
#define PREHDR_LEN 8
/* TT  - M  - I - TH - TD  */      
#define FRAME_HDR_LEN  8   

#define MSG_CONN_REQ		0x08000100
#define MSG_CONN_CONF		0x00000101
#define MSG_CONN_IND		0x00000102
#define MSG_CONN_RESP		0x08000103

#define MSG_CONN_ACTV_REQ	0x08000300
#define MSG_CONN_ACTV_CONF	0x00000301
#define MSG_CONN_ACTV_IND	0x00000302
#define MSG_CONN_ACTV_RESP	0x08000303

#define MSG_DISC_REQ		0x08000400
#define MSG_DISC_CONF		0x00000401
#define MSG_DISC_IND		0x00000402
#define MSG_DISC_RESP		0x08000403

#define MSG_TDATA_REQ		0x0908E200
#define MSG_TDATA_CONF		0x0000E201
#define MSG_TDATA_IND		0x0000E202
#define MSG_TDATA_RESP		0x0908E203

#define MSG_SELP_REQ		0x09004000
#define MSG_SELP_CONF		0x00004001

#define MSG_ACT_TRANSP_REQ      0x0908E000
#define MSG_ACT_TRANSP_CONF     0x0000E001

#define MSG_STPROT_REQ		0x09004100
#define MSG_STPROT_CONF		0x00004101

#define MSG_PING188_REQ		0x09030500
#define MSG_PING188_CONF        0x000005bc

#define MSG_WATCH188	        0x09030400

#define MSG_API_ON              0x08020102
#define MSG_POOL_PCBIT          0x08020400
#define MSG_POOL_PCBIT_CONF     0x00000401

#define MSG_INFO_IND            0x00002602
#define MSG_INFO_RESP           0x08002603

#define MSG_DEBUG_188           0x0000ff00

/*
   
   long  4 3 2 1
   Intel 1 2 3 4
*/

#ifdef __LITTLE_ENDIAN
#define SET_MSG_SCMD(msg, ch) 	(msg = (msg & 0xffffff00) | (((ch) & 0xff)))
#define SET_MSG_CMD(msg, ch) 	(msg = (msg & 0xffff00ff) | (((ch) & 0xff) << 8))
#define SET_MSG_PROC(msg, ch) 	(msg = (msg & 0xff00ffff) | (((ch) & 0xff) << 16))
#define SET_MSG_CPU(msg, ch) 	(msg = (msg & 0x00ffffff) | (((ch) & 0xff) << 24))

#define GET_MSG_SCMD(msg) 	((msg) & 0xFF)
#define GET_MSG_CMD(msg) 	((msg) >> 8 & 0xFF)
#define GET_MSG_PROC(msg) 	((msg) >> 16 & 0xFF)
#define GET_MSG_CPU(msg) 	((msg) >> 24)

#else
#error "Non-Intel CPU"
#endif

#define MAX_QUEUED 7

#define SCHED_READ    0x01
#define SCHED_WRITE   0x02

#define SET_RUN_TIMEOUT 2*HZ /* 2 seconds */
     
struct frame_buf {
        ulong msg;
        unsigned int refnum;
        unsigned int dt_len;
        unsigned int hdr_len;
        struct sk_buff *skb;
	unsigned int copied;
        struct frame_buf * next;
};

extern int pcbit_l2_write(struct pcbit_dev * dev, ulong msg, ushort refnum, 
                          struct sk_buff *skb, unsigned short hdr_len);

extern irqreturn_t pcbit_irq_handler(int interrupt, void *);

extern struct pcbit_dev * dev_pcbit[MAX_PCBIT_CARDS];

#ifdef DEBUG
static __inline__ void log_state(struct pcbit_dev *dev) {
        printk(KERN_DEBUG "writeptr = %ld\n", 
	       (ulong) (dev->writeptr - dev->sh_mem));
        printk(KERN_DEBUG "readptr  = %ld\n", 
	       (ulong) (dev->readptr - (dev->sh_mem + BANK2)));
        printk(KERN_DEBUG "{rcv_seq=%01x, send_seq=%01x, unack_seq=%01x}\n", 
	       dev->rcv_seq, dev->send_seq, dev->unack_seq);
}
#endif

static __inline__ struct pcbit_dev * chan2dev(struct pcbit_chan * chan) 
{
        struct pcbit_dev * dev;
        int i;


        for (i=0; i<MAX_PCBIT_CARDS; i++)
                if ((dev=dev_pcbit[i]))
                        if (dev->b1 == chan || dev->b2 == chan)
                                return dev;
        return NULL;

}

static __inline__ struct pcbit_dev * finddev(int id)
{
  struct pcbit_dev * dev;
  int i;

  for (i=0; i<MAX_PCBIT_CARDS; i++)
    if ((dev=dev_pcbit[i]))
      if (dev->id == id)
	return dev;
  return NULL;
}


/*
 *  Support routines for reading and writing in the board
 */

static __inline__ void pcbit_writeb(struct pcbit_dev *dev, unsigned char dt)
{
  writeb(dt, dev->writeptr++);
  if (dev->writeptr == dev->sh_mem + BANKLEN)
    dev->writeptr = dev->sh_mem;
}

static __inline__ void pcbit_writew(struct pcbit_dev *dev, unsigned short dt)
{
  int dist;

  dist = BANKLEN - (dev->writeptr - dev->sh_mem);
  switch (dist) {
  case 2:
    writew(dt, dev->writeptr);
    dev->writeptr = dev->sh_mem;
    break;
  case 1:
    writeb((u_char) (dt & 0x00ffU), dev->writeptr);    
    dev->writeptr = dev->sh_mem;
    writeb((u_char) (dt >> 8), dev->writeptr++);    
    break;
  default:
    writew(dt, dev->writeptr);
    dev->writeptr += 2;
    break;
  };
}

static __inline__ void memcpy_topcbit(struct pcbit_dev * dev, u_char * data, 
				      int len)
{
  int diff;

  diff = len - (BANKLEN - (dev->writeptr - dev->sh_mem) );

  if (diff > 0)
    {
      memcpy_toio(dev->writeptr, data, len - diff);
      memcpy_toio(dev->sh_mem, data + (len - diff), diff);
      dev->writeptr = dev->sh_mem + diff;
    }
  else
    {
      memcpy_toio(dev->writeptr, data, len);

      dev->writeptr += len;
      if (diff == 0)
	dev->writeptr = dev->sh_mem;
    }
}

static __inline__ unsigned char pcbit_readb(struct pcbit_dev *dev)
{
  unsigned char val;

  val = readb(dev->readptr++);
  if (dev->readptr == dev->sh_mem + BANK2 + BANKLEN)
    dev->readptr = dev->sh_mem + BANK2;

  return val;
}

static __inline__ unsigned short pcbit_readw(struct pcbit_dev *dev)
{
  int dist;
  unsigned short val;

  dist = BANKLEN - ( dev->readptr - (dev->sh_mem + BANK2 ) );
  switch (dist) {
  case 2:
    val = readw(dev->readptr);
    dev->readptr = dev->sh_mem + BANK2;
    break;
  case 1:
    val = readb(dev->readptr);
    dev->readptr = dev->sh_mem + BANK2;
    val = (readb(dev->readptr++) << 8) | val;
    break;
  default:
    val = readw(dev->readptr);
    dev->readptr += 2;
    break;
  };
  return val;
}

static __inline__ void memcpy_frompcbit(struct pcbit_dev * dev, u_char * data, int len)
{
  int diff;

  diff = len - (BANKLEN - (dev->readptr - (dev->sh_mem + BANK2) ) ); 
  if (diff > 0)
    {
      memcpy_fromio(data, dev->readptr, len - diff);
      memcpy_fromio(data + (len - diff), dev->sh_mem + BANK2 , diff);
      dev->readptr = dev->sh_mem + BANK2 + diff;
    }
  else
    {
      memcpy_fromio(data, dev->readptr, len);
      dev->readptr += len;
      if (diff == 0)
	dev->readptr = dev->sh_mem + BANK2;
    }
}


#endif







