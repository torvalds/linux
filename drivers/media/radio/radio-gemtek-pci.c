/*
 ***************************************************************************
 *     
 *     radio-gemtek-pci.c - Gemtek PCI Radio driver
 *     (C) 2001 Vladimir Shebordaev <vshebordaev@mail.ru>
 *
 ***************************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 ***************************************************************************
 *
 *     Gemtek Corp still silently refuses to release any specifications
 *     of their multimedia devices, so the protocol still has to be
 *     reverse engineered.
 *
 *     The v4l code was inspired by Jonas Munsin's  Gemtek serial line
 *     radio device driver.
 *
 *     Please, let me know if this piece of code was useful :)
 * 
 *     TODO: multiple device support and portability were not tested
 *
 ***************************************************************************
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/videodev.h>
#include <linux/errno.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#ifndef PCI_VENDOR_ID_GEMTEK
#define PCI_VENDOR_ID_GEMTEK 0x5046
#endif

#ifndef PCI_DEVICE_ID_GEMTEK_PR103
#define PCI_DEVICE_ID_GEMTEK_PR103 0x1001
#endif

#ifndef GEMTEK_PCI_RANGE_LOW
#define GEMTEK_PCI_RANGE_LOW (87*16000)
#endif

#ifndef GEMTEK_PCI_RANGE_HIGH
#define GEMTEK_PCI_RANGE_HIGH (108*16000)
#endif

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE 
#define FALSE (0)
#endif

struct gemtek_pci_card {
	struct video_device *videodev;
	
	u32 iobase;
	u32 length;
	u8  chiprev;
	u16 model;
	
	u32 current_frequency;
	u8  mute;
};

static const char rcsid[] = "$Id: radio-gemtek-pci.c,v 1.1 2001/07/23 08:08:16 ted Exp ted $";

static int nr_radio = -1;

static inline u8 gemtek_pci_out( u16 value, u32 port )
{
	outw( value, port );

	return (u8)value;
}

#define _b0( v ) *((u8 *)&v)  
static void __gemtek_pci_cmd( u16 value, u32 port, u8 *last_byte, int keep )
{
	register u8 byte = *last_byte;

	if ( !value ) {
		if ( !keep )
			value = (u16)port;
		byte &= 0xfd;	
	} else
		byte |= 2;

	_b0( value ) = byte;
	outw( value, port );
	byte |= 1;
	_b0( value ) = byte;
	outw( value, port );
	byte &= 0xfe;
	_b0( value ) = byte;
	outw( value, port );
	
	*last_byte = byte;
}

static inline void gemtek_pci_nil( u32 port, u8 *last_byte )
{
	__gemtek_pci_cmd( 0x00, port, last_byte, FALSE );
}

static inline void gemtek_pci_cmd( u16 cmd, u32 port, u8 *last_byte )
{
	__gemtek_pci_cmd( cmd, port, last_byte, TRUE );
}

static void gemtek_pci_setfrequency( struct gemtek_pci_card *card, unsigned long frequency )
{
	register int i;
	register u32 value = frequency / 200 + 856;
	register u16 mask = 0x8000;
	u8 last_byte;
	u32 port = card->iobase;

	last_byte = gemtek_pci_out( 0x06, port );

	i = 0;
	do {
		gemtek_pci_nil( port, &last_byte );
		i++;
	} while ( i < 9 );

	i = 0;
	do {
		gemtek_pci_cmd( value & mask, port, &last_byte );
		mask >>= 1;
		i++;
	} while ( i < 16 );

	outw( 0x10, port );
}


static inline void gemtek_pci_mute( struct gemtek_pci_card *card )
{
	outb( 0x1f, card->iobase );
	card->mute = TRUE;
}

static inline void gemtek_pci_unmute( struct gemtek_pci_card *card )
{
	if ( card->mute ) {
		gemtek_pci_setfrequency( card, card->current_frequency );
		card->mute = FALSE;
	}
}

static inline unsigned int gemtek_pci_getsignal( struct gemtek_pci_card *card )
{
	return ( inb( card->iobase ) & 0x08 ) ? 0 : 1;
}

static int gemtek_pci_do_ioctl(struct inode *inode, struct file *file,
			       unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct gemtek_pci_card *card = dev->priv;

	switch ( cmd ) {
		case VIDIOCGCAP:
		{
			struct video_capability *c = arg;

			memset(c,0,sizeof(*c));
			c->type = VID_TYPE_TUNER;
			c->channels = 1;
			c->audios = 1;
			strcpy( c->name, "Gemtek PCI Radio" );
			return 0;
		} 

		case VIDIOCGTUNER:
		{
			struct video_tuner *t = arg;

			if ( t->tuner ) 
				return -EINVAL;

			t->rangelow = GEMTEK_PCI_RANGE_LOW;
			t->rangehigh = GEMTEK_PCI_RANGE_HIGH;
			t->flags = VIDEO_TUNER_LOW;
			t->mode = VIDEO_MODE_AUTO;
			t->signal = 0xFFFF * gemtek_pci_getsignal( card );
			strcpy( t->name, "FM" );
			return 0;
		}

		case VIDIOCSTUNER:
		{
			struct video_tuner *t = arg;
			if ( t->tuner )
				return -EINVAL;
			return 0;
		}

		case VIDIOCGFREQ:
		{
			unsigned long *freq = arg;
			*freq = card->current_frequency;
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long *freq = arg;
	 
			if ( (*freq < GEMTEK_PCI_RANGE_LOW) ||
			     (*freq > GEMTEK_PCI_RANGE_HIGH) )
				return -EINVAL;

			gemtek_pci_setfrequency( card, *freq );
			card->current_frequency = *freq;
			card->mute = FALSE;

			return 0;
		}
  
		case VIDIOCGAUDIO:
		{	
			struct video_audio *a = arg;

			memset( a, 0, sizeof( *a ) );
			a->flags |= VIDEO_AUDIO_MUTABLE;
			a->volume = 1;
			a->step = 65535;
			strcpy( a->name, "Radio" );
			return 0;			
		}

		case VIDIOCSAUDIO:
		{
			struct video_audio *a = arg;

			if ( a->audio ) 
				return -EINVAL;

			if ( a->flags & VIDEO_AUDIO_MUTE ) 
				gemtek_pci_mute( card );
			else
				gemtek_pci_unmute( card );
			return 0;
		}

		default:
			return -ENOIOCTLCMD;
	}
}

static int gemtek_pci_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, gemtek_pci_do_ioctl);
}

enum {
	GEMTEK_PR103
};

static char *card_names[] __devinitdata = {
	"GEMTEK_PR103"
};

static struct pci_device_id gemtek_pci_id[] =
{
	{ PCI_VENDOR_ID_GEMTEK, PCI_DEVICE_ID_GEMTEK_PR103,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, GEMTEK_PR103 },
	{ 0 }
};

MODULE_DEVICE_TABLE( pci, gemtek_pci_id );

static int mx = 1;

static struct file_operations gemtek_pci_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= gemtek_pci_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device vdev_template = {
	.owner         = THIS_MODULE,
	.name          = "Gemtek PCI Radio",
	.type          = VID_TYPE_TUNER,
	.hardware      = VID_HARDWARE_GEMTEK,
	.fops          = &gemtek_pci_fops,
};

static int __devinit gemtek_pci_probe( struct pci_dev *pci_dev, const struct pci_device_id *pci_id )
{
	struct gemtek_pci_card *card;
	struct video_device *devradio;

	if ( (card = kzalloc( sizeof( struct gemtek_pci_card ), GFP_KERNEL )) == NULL ) {
		printk( KERN_ERR "gemtek_pci: out of memory\n" );
		return -ENOMEM;
	}

	if ( pci_enable_device( pci_dev ) ) 
		goto err_pci;
	
	card->iobase = pci_resource_start( pci_dev, 0 );
	card->length = pci_resource_len( pci_dev, 0 );

	if ( request_region( card->iobase, card->length, card_names[pci_id->driver_data] ) == NULL ) {
		printk( KERN_ERR "gemtek_pci: i/o port already in use\n" );
		goto err_pci;
	}

	pci_read_config_byte( pci_dev, PCI_REVISION_ID, &card->chiprev );
	pci_read_config_word( pci_dev, PCI_SUBSYSTEM_ID, &card->model );

	pci_set_drvdata( pci_dev, card );
 
	if ( (devradio = kmalloc( sizeof( struct video_device ), GFP_KERNEL )) == NULL ) {
		printk( KERN_ERR "gemtek_pci: out of memory\n" );
		goto err_video;
	}
	*devradio = vdev_template;

	if ( video_register_device( devradio, VFL_TYPE_RADIO , nr_radio) == -1 ) {
		kfree( devradio );
		goto err_video;
	}

	card->videodev = devradio;
	devradio->priv = card;
	gemtek_pci_mute( card );

	printk( KERN_INFO "Gemtek PCI Radio (rev. %d) found at 0x%04x-0x%04x.\n", 
		card->chiprev, card->iobase, card->iobase + card->length - 1 );

	return 0;

err_video:
	release_region( card->iobase, card->length );

err_pci:
	kfree( card );
	return -ENODEV;        
}

static void __devexit gemtek_pci_remove( struct pci_dev *pci_dev )
{
	struct gemtek_pci_card *card = pci_get_drvdata( pci_dev );

	video_unregister_device( card->videodev );
	kfree( card->videodev );

	release_region( card->iobase, card->length );
	
	if ( mx )
		gemtek_pci_mute( card );

	kfree( card );
	
	pci_set_drvdata( pci_dev, NULL );
}

static struct pci_driver gemtek_pci_driver =
{
	.name		= "gemtek_pci",
	.id_table	= gemtek_pci_id,
	.probe		= gemtek_pci_probe,
	.remove		= __devexit_p(gemtek_pci_remove),
};

static int __init gemtek_pci_init_module( void )
{
	return pci_register_driver( &gemtek_pci_driver );
}

static void __exit gemtek_pci_cleanup_module( void )
{
	return pci_unregister_driver( &gemtek_pci_driver );
}

MODULE_AUTHOR( "Vladimir Shebordaev <vshebordaev@mail.ru>" );
MODULE_DESCRIPTION( "The video4linux driver for the Gemtek PCI Radio Card" );
MODULE_LICENSE("GPL");

module_param(mx, bool, 0);
MODULE_PARM_DESC( mx, "single digit: 1 - turn off the turner upon module exit (default), 0 - do not" );
module_param(nr_radio, int, 0);
MODULE_PARM_DESC( nr_radio, "video4linux device number to use");

module_init( gemtek_pci_init_module );
module_exit( gemtek_pci_cleanup_module );

