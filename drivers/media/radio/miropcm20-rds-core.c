/*
 *  Many thanks to Fred Seidel <seidel@metabox.de>, the
 *  designer of the RDS decoder hardware. With his help
 *  I was able to code this driver.
 *  Thanks also to Norberto Pellicci, Dominic Mounteney
 *  <DMounteney@pinnaclesys.com> and www.teleauskunft.de
 *  for good hints on finding Fred. It was somewhat hard
 *  to locate him here in Germany... [:
 *
 * Revision history:
 *
 *   2000-08-09  Robert Siemer <Robert.Siemer@gmx.de>
 *        RDS support for MiroSound PCM20 radio
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/semaphore.h>
#include <asm/io.h>
#include "../../../sound/oss/aci.h"
#include "miropcm20-rds-core.h"

#define DEBUG 0

static struct semaphore aci_rds_sem;

#define RDS_DATASHIFT          2   /* Bit 2 */
#define RDS_DATAMASK        (1 << RDS_DATASHIFT)
#define RDS_BUSYMASK        0x10   /* Bit 4 */
#define RDS_CLOCKMASK       0x08   /* Bit 3 */

#define RDS_DATA(x)         (((x) >> RDS_DATASHIFT) & 1) 


#if DEBUG
static void print_matrix(char array[], unsigned int length)
{
        int i, j;

        for (i=0; i<length; i++) {
                printk(KERN_DEBUG "aci-rds: ");
                for (j=7; j>=0; j--) {
                        printk("%d", (array[i] >> j) & 0x1);
                }
                if (i%8 == 0)
                        printk(" byte-border\n");
                else
                        printk("\n");
        }
}
#endif /* DEBUG */

static int byte2trans(unsigned char byte, unsigned char sendbuffer[], int size)
{
	int i;

	if (size != 8)
		return -1;
	for (i = 7; i >= 0; i--)
		sendbuffer[7-i] = (byte & (1 << i)) ? RDS_DATAMASK : 0;
	sendbuffer[0] |= RDS_CLOCKMASK;

	return 0;
}

static int rds_waitread(void)
{
	unsigned char byte;
	int i=2000;

	do {
		byte=inb(RDS_REGISTER);
		i--;
	}
	while ((byte & RDS_BUSYMASK) && i);

	if (i) {
		#if DEBUG
		printk(KERN_DEBUG "rds_waitread()");
		print_matrix(&byte, 1);
		#endif
		return (byte);
	} else {
		printk(KERN_WARNING "aci-rds: rds_waitread() timeout...\n");
		return -1;
	}
}

/* don't use any ..._nowait() function if you are not sure what you do... */

static inline void rds_rawwrite_nowait(unsigned char byte)
{
	#if DEBUG
	printk(KERN_DEBUG "rds_rawwrite()");
	print_matrix(&byte, 1);
	#endif
	outb(byte, RDS_REGISTER);
}

static int rds_rawwrite(unsigned char byte)
{
	if (rds_waitread() >= 0) {
		rds_rawwrite_nowait(byte);
		return 0;
	} else
		return -1;
}

static int rds_write(unsigned char cmd)
{
	unsigned char sendbuffer[8];
	int i;
	
	if (byte2trans(cmd, sendbuffer, 8) != 0){
		return -1;
	} else {
		for (i=0; i<8; i++) {
			rds_rawwrite(sendbuffer[i]);
		}
	}
	return 0;
}

static int rds_readcycle_nowait(void)
{
	rds_rawwrite_nowait(0);
	return rds_waitread();
}

static int rds_readcycle(void)
{
	if (rds_rawwrite(0) < 0)
		return -1;
	return rds_waitread();
}

static int rds_read(unsigned char databuffer[], int datasize)
{
	#define READSIZE (8*datasize)

	int i,j;

	if (datasize < 1)  /* nothing to read */
		return 0;

	/* to be able to use rds_readcycle_nowait()
	   I have to waitread() here */
	if (rds_waitread() < 0)
		return -1;
	
	memset(databuffer, 0, datasize);

	for (i=0; i< READSIZE; i++)
		if((j=rds_readcycle_nowait()) < 0) {
			return -1;
		} else {
			databuffer[i/8]|=(RDS_DATA(j) << (7-(i%8)));
		}

	return 0;
}

static int rds_ack(void)
{
	int i=rds_readcycle();

	if (i < 0)
		return -1;
	if (i & RDS_DATAMASK) {
		return 0;  /* ACK  */
	} else {
		printk(KERN_DEBUG "aci-rds: NACK\n");
		return 1;  /* NACK */
	}
}

int aci_rds_cmd(unsigned char cmd, unsigned char databuffer[], int datasize)
{
	int ret;

	if (down_interruptible(&aci_rds_sem))
		return -EINTR;

	rds_write(cmd);

	/* RDS_RESET doesn't need further processing */
	if (cmd!=RDS_RESET && (rds_ack() || rds_read(databuffer, datasize)))
		ret = -1;
	else
		ret = 0;

	up(&aci_rds_sem);
	
	return ret;
}
EXPORT_SYMBOL(aci_rds_cmd);

int __init attach_aci_rds(void)
{
	init_MUTEX(&aci_rds_sem);
	return 0;
}

void __exit unload_aci_rds(void)
{
}
MODULE_LICENSE("GPL");
