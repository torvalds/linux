/*
 * DVB USB Linux driver for Afatech AF9015 DVB-T USB2.0 receiver
 *
 * Copyright (C) 2007 Antti Palosaari <crope@iki.fi>
 *
 * Thanks to Afatech who kindly provided information.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _DVB_USB_AF9015_H_
#define _DVB_USB_AF9015_H_

#define DVB_USB_LOG_PREFIX "af9015"
#include "dvb-usb.h"

#define deb_info(args...) dprintk(dvb_usb_af9015_debug, 0x01, args)
#define deb_rc(args...)   dprintk(dvb_usb_af9015_debug, 0x02, args)
#define deb_xfer(args...) dprintk(dvb_usb_af9015_debug, 0x04, args)
#define deb_reg(args...)  dprintk(dvb_usb_af9015_debug, 0x08, args)
#define deb_i2c(args...)  dprintk(dvb_usb_af9015_debug, 0x10, args)
#define deb_fw(args...)   dprintk(dvb_usb_af9015_debug, 0x20, args)

#define AF9015_I2C_EEPROM  0xa0
#define AF9015_I2C_DEMOD   0x38
#define AF9015_USB_TIMEOUT 2000

/* EEPROM locations */
#define AF9015_EEPROM_IR_MODE        0x18
#define AF9015_EEPROM_IR_REMOTE_TYPE 0x34
#define AF9015_EEPROM_TS_MODE        0x31
#define AF9015_EEPROM_DEMOD2_I2C     0x32

#define AF9015_EEPROM_SAW_BW1        0x35
#define AF9015_EEPROM_XTAL_TYPE1     0x36
#define AF9015_EEPROM_SPEC_INV1      0x37
#define AF9015_EEPROM_IF1L           0x38
#define AF9015_EEPROM_IF1H           0x39
#define AF9015_EEPROM_MT2060_IF1L    0x3a
#define AF9015_EEPROM_MT2060_IF1H    0x3b
#define AF9015_EEPROM_TUNER_ID1      0x3c

#define AF9015_EEPROM_SAW_BW2        0x45
#define AF9015_EEPROM_XTAL_TYPE2     0x46
#define AF9015_EEPROM_SPEC_INV2      0x47
#define AF9015_EEPROM_IF2L           0x48
#define AF9015_EEPROM_IF2H           0x49
#define AF9015_EEPROM_MT2060_IF2L    0x4a
#define AF9015_EEPROM_MT2060_IF2H    0x4b
#define AF9015_EEPROM_TUNER_ID2      0x4c

#define AF9015_EEPROM_OFFSET (AF9015_EEPROM_SAW_BW2 - AF9015_EEPROM_SAW_BW1)

struct req_t {
	u8  cmd;       /* [0] */
	/*  seq */     /* [1] */
	u8  i2c_addr;  /* [2] */
	u16 addr;      /* [3|4] */
	u8  mbox;      /* [5] */
	u8  addr_len;  /* [6] */
	u8  data_len;  /* [7] */
	u8  *data;
};

enum af9015_cmd {
	GET_CONFIG           = 0x10,
	DOWNLOAD_FIRMWARE    = 0x11,
	BOOT                 = 0x13,
	READ_MEMORY          = 0x20,
	WRITE_MEMORY         = 0x21,
	READ_WRITE_I2C       = 0x22,
	COPY_FIRMWARE        = 0x23,
	RECONNECT_USB        = 0x5a,
	WRITE_VIRTUAL_MEMORY = 0x26,
	GET_IR_CODE          = 0x27,
	READ_I2C,
	WRITE_I2C,
};

enum af9015_ir_mode {
	AF9015_IR_MODE_DISABLED = 0,
	AF9015_IR_MODE_HID,
	AF9015_IR_MODE_RLC,
	AF9015_IR_MODE_RC6,
	AF9015_IR_MODE_POLLING, /* just guess */
};

struct af9015_state {
	struct i2c_adapter i2c_adap; /* I2C adapter for 2nd FE */
	u8 rc_repeat;
};

struct af9015_config {
	u8  dual_mode:1;
	u16 mt2060_if1[2];
	u16 firmware_size;
	u16 firmware_checksum;
	u32 eeprom_sum;
};

enum af9015_remote {
	AF9015_REMOTE_NONE                    = 0,
/* 1 */	AF9015_REMOTE_A_LINK_DTU_M,
	AF9015_REMOTE_MSI_DIGIVOX_MINI_II_V3,
	AF9015_REMOTE_MYGICTV_U718,
	AF9015_REMOTE_DIGITTRADE_DVB_T,
/* 5 */	AF9015_REMOTE_AVERMEDIA_KS,
};

/* LeadTek - Y04G0051 */
/* Leadtek WinFast DTV Dongle Gold */
static struct ir_scancode af9015_rc_leadtek[] = {
	{ 0x0300, KEY_POWER2 },
	{ 0x0303, KEY_SCREEN },
	{ 0x0304, KEY_RIGHT },
	{ 0x0305, KEY_1 },
	{ 0x0306, KEY_2 },
	{ 0x0307, KEY_3 },
	{ 0x0308, KEY_LEFT },
	{ 0x0309, KEY_4 },
	{ 0x030a, KEY_5 },
	{ 0x030b, KEY_6 },
	{ 0x030c, KEY_UP },
	{ 0x030d, KEY_7 },
	{ 0x030e, KEY_8 },
	{ 0x030f, KEY_9 },
	{ 0x0310, KEY_DOWN },
	{ 0x0311, KEY_AGAIN },
	{ 0x0312, KEY_0 },
	{ 0x0313, KEY_OK },              /* 1st ok */
	{ 0x0314, KEY_MUTE },
	{ 0x0316, KEY_OK },              /* 2nd ok */
	{ 0x031e, KEY_VIDEO },           /* 2nd video */
	{ 0x031b, KEY_AUDIO },
	{ 0x031f, KEY_TEXT },
	{ 0x0340, KEY_SLEEP },
	{ 0x0341, KEY_DOT },
	{ 0x0342, KEY_REWIND },
	{ 0x0343, KEY_PLAY },
	{ 0x0344, KEY_FASTFORWARD },
	{ 0x0345, KEY_TIME },
	{ 0x0346, KEY_STOP },            /* 2nd stop */
	{ 0x0347, KEY_RECORD },
	{ 0x0348, KEY_CAMERA },
	{ 0x0349, KEY_ESC },
	{ 0x034a, KEY_NEW },
	{ 0x034b, KEY_RED },
	{ 0x034c, KEY_GREEN },
	{ 0x034d, KEY_YELLOW },
	{ 0x034e, KEY_BLUE },
	{ 0x034f, KEY_MENU },
	{ 0x0350, KEY_STOP },            /* 1st stop */
	{ 0x0351, KEY_CHANNEL },
	{ 0x0352, KEY_VIDEO },           /* 1st video */
	{ 0x0353, KEY_EPG },
	{ 0x0354, KEY_PREVIOUS },
	{ 0x0355, KEY_NEXT },
	{ 0x0356, KEY_TV },
	{ 0x035a, KEY_VOLUMEDOWN },
	{ 0x035b, KEY_CHANNELUP },
	{ 0x035e, KEY_VOLUMEUP },
	{ 0x035f, KEY_CHANNELDOWN },
};

/* TwinHan AzureWave AD-TU700(704J) */
static struct ir_scancode af9015_rc_twinhan[] = {
	{ 0x0000, KEY_TAB },             /* Tab */
	{ 0x0001, KEY_2 },
	{ 0x0002, KEY_CHANNELDOWN },
	{ 0x0003, KEY_1 },
	{ 0x0004, KEY_LIST },            /* Record List */
	{ 0x0005, KEY_CHANNELUP },
	{ 0x0006, KEY_3 },
	{ 0x0007, KEY_SLEEP },           /* Hibernate */
	{ 0x0008, KEY_SWITCHVIDEOMODE }, /* A/V */
	{ 0x0009, KEY_4 },
	{ 0x000a, KEY_VOLUMEDOWN },
	{ 0x000c, KEY_CANCEL },          /* Cancel */
	{ 0x000d, KEY_7 },
	{ 0x000e, KEY_AGAIN },           /* Recall */
	{ 0x000f, KEY_TEXT },            /* Teletext */
	{ 0x0010, KEY_MUTE },
	{ 0x0011, KEY_RECORD },
	{ 0x0012, KEY_FASTFORWARD },     /* FF >> */
	{ 0x0013, KEY_BACK },            /* Back */
	{ 0x0014, KEY_PLAY },
	{ 0x0015, KEY_0 },
	{ 0x0016, KEY_POWER },
	{ 0x0017, KEY_FAVORITES },       /* Favorite List */
	{ 0x0018, KEY_RED },
	{ 0x0019, KEY_8 },
	{ 0x001a, KEY_STOP },
	{ 0x001b, KEY_9 },
	{ 0x001c, KEY_EPG },             /* Info/EPG */
	{ 0x001d, KEY_5 },
	{ 0x001e, KEY_VOLUMEUP },
	{ 0x001f, KEY_6 },
	{ 0x0040, KEY_REWIND },          /* FR << */
	{ 0x0041, KEY_PREVIOUS },        /* Replay */
	{ 0x0042, KEY_NEXT },            /* Skip */
	{ 0x0043, KEY_SUBTITLE },        /* Subtitle / CC */
	{ 0x0045, KEY_KPPLUS },          /* Zoom+ */
	{ 0x0046, KEY_KPMINUS },         /* Zoom- */
	{ 0x0047, KEY_TV2 },             /* PIP */
	{ 0x0048, KEY_INFO },            /* Preview */
	{ 0x0049, KEY_AUDIO },           /* L/R */ /* TODO better event */
	{ 0x004a, KEY_CLEAR },           /* Clear */
	{ 0x004b, KEY_UP },              /* up arrow */
	{ 0x004c, KEY_PAUSE },
	{ 0x004d, KEY_ZOOM },            /* Full Screen */
	{ 0x004e, KEY_LEFT },            /* left arrow */
	{ 0x004f, KEY_ENTER },           /* Enter / ok */
	{ 0x0050, KEY_LANGUAGE },        /* SAP */
	{ 0x0051, KEY_DOWN },            /* down arrow */
	{ 0x0052, KEY_RIGHT },           /* right arrow */
	{ 0x0053, KEY_GREEN },
	{ 0x0054, KEY_CAMERA },          /* Capture */
	{ 0x005e, KEY_YELLOW },
	{ 0x005f, KEY_BLUE },
};

/* A-Link DTU(m) - 3x6 slim remote */
static struct ir_scancode af9015_rc_a_link[] = {
	{ 0x0800, KEY_VOLUMEUP },
	{ 0x0801, KEY_1 },
	{ 0x0802, KEY_3 },
	{ 0x0803, KEY_7 },
	{ 0x0804, KEY_9 },
	{ 0x0805, KEY_ZOOM },
	{ 0x0806, KEY_0 },
	{ 0x0807, KEY_GOTO },            /* jump */
	{ 0x080d, KEY_5 },
	{ 0x080f, KEY_2 },
	{ 0x0812, KEY_POWER },
	{ 0x0814, KEY_CHANNELUP },
	{ 0x0816, KEY_VOLUMEDOWN },
	{ 0x0818, KEY_6 },
	{ 0x081a, KEY_MUTE },
	{ 0x081b, KEY_8 },
	{ 0x081c, KEY_4 },
	{ 0x081d, KEY_CHANNELDOWN },
};

/* MSI DIGIVOX mini II V3.0 */
static struct ir_scancode af9015_rc_msi[] = {
	{ 0x0002, KEY_2 },
	{ 0x0003, KEY_UP },              /* up */
	{ 0x0004, KEY_3 },
	{ 0x0005, KEY_CHANNELDOWN },
	{ 0x0008, KEY_5 },
	{ 0x0009, KEY_0 },
	{ 0x000b, KEY_8 },
	{ 0x000d, KEY_DOWN },            /* down */
	{ 0x0010, KEY_9 },
	{ 0x0011, KEY_7 },
	{ 0x0014, KEY_VOLUMEUP },
	{ 0x0015, KEY_CHANNELUP },
	{ 0x0016, KEY_ENTER },
	{ 0x0017, KEY_POWER },
	{ 0x001a, KEY_1 },
	{ 0x001c, KEY_4 },
	{ 0x001d, KEY_6 },
	{ 0x001f, KEY_VOLUMEDOWN },
};

/* MYGICTV U718 */
/* Uses NEC extended 0x02bd. Extended byte removed for compatibility... */
static struct ir_scancode af9015_rc_mygictv[] = {
	{ 0x0200, KEY_1 },
	{ 0x0201, KEY_2 },
	{ 0x0202, KEY_3 },
	{ 0x0203, KEY_4 },
	{ 0x0204, KEY_5 },
	{ 0x0205, KEY_6 },
	{ 0x0206, KEY_7 },
	{ 0x0207, KEY_8 },
	{ 0x0208, KEY_9 },
	{ 0x0209, KEY_0 },
	{ 0x020a, KEY_MUTE },
	{ 0x020b, KEY_CYCLEWINDOWS },    /* yellow, min / max */
	{ 0x020c, KEY_SWITCHVIDEOMODE }, /* TV / AV */
	{ 0x020e, KEY_VOLUMEDOWN },
	{ 0x020f, KEY_TIME },            /* TimeShift */
	{ 0x0210, KEY_RIGHT },           /* right arrow */
	{ 0x0211, KEY_LEFT },            /* left arrow */
	{ 0x0212, KEY_UP },              /* up arrow */
	{ 0x0213, KEY_DOWN },            /* down arrow */
	{ 0x0214, KEY_POWER },
	{ 0x0215, KEY_ENTER },           /* ok */
	{ 0x0216, KEY_STOP },
	{ 0x0217, KEY_CAMERA },          /* Snapshot */
	{ 0x0218, KEY_CHANNELUP },
	{ 0x0219, KEY_RECORD },
	{ 0x021a, KEY_CHANNELDOWN },
	{ 0x021c, KEY_ESC },             /* Esc */
	{ 0x021e, KEY_PLAY },
	{ 0x021f, KEY_VOLUMEUP },
	{ 0x0240, KEY_PAUSE },
	{ 0x0241, KEY_FASTFORWARD },     /* FF >> */
	{ 0x0242, KEY_REWIND },          /* FR << */
	{ 0x0243, KEY_ZOOM },            /* 'select' (?) */
	{ 0x0244, KEY_SHUFFLE },         /* Shuffle */
	{ 0x0245, KEY_POWER },
};

/* KWorld PlusTV Dual DVB-T Stick (DVB-T 399U) */
/* FIXME: This mapping is totally incomplete and probably even wrong... */
/* Uses NEC extended 0x866b. Extended byte removed for compatibility... */
static struct ir_scancode af9015_rc_kworld[] = {
	{ 0x8600, KEY_1 },
	{ 0x8601, KEY_2 },
	{ 0x8602, KEY_3 },
	{ 0x8603, KEY_4 },
	{ 0x8604, KEY_5 },
	{ 0x8605, KEY_6 },
	{ 0x8606, KEY_7 },
	{ 0x8607, KEY_8 },
	{ 0x8608, KEY_9 },
	{ 0x860a, KEY_0 },
};

/* AverMedia Volar X */
static struct ir_scancode af9015_rc_avermedia[] = {
	{ 0x0200, KEY_POWER },           /* POWER */
	{ 0x0201, KEY_PROG1 },           /* SOURCE */
	{ 0x0203, KEY_TEXT },            /* TELETEXT */
	{ 0x0204, KEY_EPG },             /* EPG */
	{ 0x0205, KEY_1 },               /* 1 */
	{ 0x0206, KEY_2 },               /* 2 */
	{ 0x0207, KEY_3 },               /* 3 */
	{ 0x0208, KEY_AUDIO },           /* AUDIO */
	{ 0x0209, KEY_4 },               /* 4 */
	{ 0x020a, KEY_5 },               /* 5 */
	{ 0x020b, KEY_6 },               /* 6 */
	{ 0x020c, KEY_ZOOM },            /* FULL SCREEN */
	{ 0x020d, KEY_7 },               /* 7 */
	{ 0x020e, KEY_8 },               /* 8 */
	{ 0x020f, KEY_9 },               /* 9 */
	{ 0x0210, KEY_PROG3 },           /* 16-CH PREV */
	{ 0x0211, KEY_0 },               /* 0 */
	{ 0x0212, KEY_LEFT },            /* L / DISPLAY */
	{ 0x0213, KEY_RIGHT },           /* R / CH RTN */
	{ 0x0214, KEY_MUTE },            /* MUTE */
	{ 0x0215, KEY_MENU },            /* MENU */
	{ 0x0217, KEY_PROG2 },           /* SNAP SHOT */
	{ 0x0218, KEY_PLAY },            /* PLAY */
	{ 0x0219, KEY_RECORD },          /* RECORD */
	{ 0x021a, KEY_PLAYPAUSE },       /* TIMESHIFT / PAUSE */
	{ 0x021b, KEY_STOP },            /* STOP */
	{ 0x021c, KEY_FORWARD },         /* >> / YELLOW */
	{ 0x021d, KEY_BACK },            /* << / RED */
	{ 0x021e, KEY_VOLUMEDOWN },      /* VOL DOWN */
	{ 0x021f, KEY_VOLUMEUP },        /* VOL UP */

	{ 0x0300, KEY_LAST },            /* >>| / BLUE */
	{ 0x0301, KEY_FIRST },           /* |<< / GREEN */
	{ 0x0302, KEY_CHANNELDOWN },     /* CH DOWN */
	{ 0x0303, KEY_CHANNELUP },       /* CH UP */
};

/* AverMedia KS */
/* FIXME: mappings are not 100% correct? */
static struct ir_scancode af9015_rc_avermedia_ks[] = {
	{ 0x0501, KEY_POWER },
	{ 0x0502, KEY_CHANNELUP },
	{ 0x0503, KEY_CHANNELDOWN },
	{ 0x0504, KEY_VOLUMEUP },
	{ 0x0505, KEY_VOLUMEDOWN },
	{ 0x0506, KEY_MUTE },
	{ 0x0507, KEY_RIGHT },
	{ 0x0508, KEY_PROG1 },
	{ 0x0509, KEY_1 },
	{ 0x050a, KEY_2 },
	{ 0x050b, KEY_3 },
	{ 0x050c, KEY_4 },
	{ 0x050d, KEY_5 },
	{ 0x050e, KEY_6 },
	{ 0x050f, KEY_7 },
	{ 0x0510, KEY_8 },
	{ 0x0511, KEY_9 },
	{ 0x0512, KEY_0 },
	{ 0x0513, KEY_AUDIO },
	{ 0x0515, KEY_EPG },
	{ 0x0516, KEY_PLAY },
	{ 0x0517, KEY_RECORD },
	{ 0x0518, KEY_STOP },
	{ 0x051c, KEY_BACK },
	{ 0x051d, KEY_FORWARD },
	{ 0x054d, KEY_LEFT },
	{ 0x0556, KEY_ZOOM },
};

/* Digittrade DVB-T USB Stick */
static struct ir_scancode af9015_rc_digittrade[] = {
	{ 0x0000, KEY_9 },
	{ 0x0001, KEY_EPG },             /* EPG */
	{ 0x0002, KEY_VOLUMEDOWN },      /* Vol Dn */
	{ 0x0003, KEY_TEXT },            /* TELETEXT */
	{ 0x0004, KEY_8 },
	{ 0x0005, KEY_MUTE },            /* MUTE */
	{ 0x0006, KEY_POWER },           /* POWER */
	{ 0x0009, KEY_ZOOM },            /* FULLSCREEN */
	{ 0x000a, KEY_RECORD },          /* RECORD */
	{ 0x000d, KEY_SUBTITLE },        /* SUBTITLE */
	{ 0x000e, KEY_STOP },            /* STOP */
	{ 0x0010, KEY_LAST },            /* RETURN */
	{ 0x0011, KEY_2 },
	{ 0x0012, KEY_4 },
	{ 0x0015, KEY_3 },
	{ 0x0016, KEY_5 },
	{ 0x0017, KEY_CHANNELDOWN },     /* Ch Dn */
	{ 0x0019, KEY_CHANNELUP },       /* CH Up */
	{ 0x001a, KEY_PAUSE },           /* PAUSE */
	{ 0x001b, KEY_1 },
	{ 0x001d, KEY_AUDIO },           /* DUAL SOUND */
	{ 0x001e, KEY_PLAY },            /* PLAY */
	{ 0x001f, KEY_PRINT },           /* SNAPSHOT */
	{ 0x0040, KEY_VOLUMEUP },        /* Vol Up */
	{ 0x0048, KEY_7 },
	{ 0x004c, KEY_6 },
	{ 0x004d, KEY_PLAYPAUSE },       /* TIMESHIFT */
	{ 0x0054, KEY_0 },
};

/* TREKSTOR DVB-T USB Stick */
static struct ir_scancode af9015_rc_trekstor[] = {
	{ 0x0084, KEY_0 },
	{ 0x0085, KEY_MUTE },            /* Mute */
	{ 0x0086, KEY_AGAIN },           /* Home */
	{ 0x0087, KEY_UP },              /* Up */
	{ 0x0088, KEY_ENTER },           /* OK */
	{ 0x0089, KEY_RIGHT },           /* Right */
	{ 0x008a, KEY_FASTFORWARD },     /* Fast forward */
	{ 0x008b, KEY_VOLUMEUP },        /* Volume + */
	{ 0x008c, KEY_DOWN },            /* Down */
	{ 0x008d, KEY_PLAY },            /* Play/Pause */
	{ 0x008e, KEY_STOP },            /* Stop */
	{ 0x008f, KEY_EPG },             /* Info/EPG */
	{ 0x0090, KEY_7 },
	{ 0x0091, KEY_4 },
	{ 0x0092, KEY_1 },
	{ 0x0093, KEY_CHANNELDOWN },     /* Channel - */
	{ 0x0094, KEY_8 },
	{ 0x0095, KEY_5 },
	{ 0x0096, KEY_2 },
	{ 0x0097, KEY_CHANNELUP },       /* Channel + */
	{ 0x0098, KEY_9 },
	{ 0x0099, KEY_6 },
	{ 0x009a, KEY_3 },
	{ 0x009b, KEY_VOLUMEDOWN },      /* Volume - */
	{ 0x009c, KEY_ZOOM },            /* TV */
	{ 0x009d, KEY_RECORD },          /* Record */
	{ 0x009e, KEY_REWIND },          /* Rewind */
	{ 0x009f, KEY_LEFT },            /* Left */
};

/* MSI DIGIVOX mini III */
/* Uses NEC extended 0x61d6. Extended byte removed for compatibility... */
static struct ir_scancode af9015_rc_msi_digivox_iii[] = {
	{ 0x6101, KEY_VIDEO },           /* Source */
	{ 0x6102, KEY_3 },
	{ 0x6103, KEY_POWER2 },          /* ShutDown */
	{ 0x6104, KEY_1 },
	{ 0x6105, KEY_5 },
	{ 0x6106, KEY_6 },
	{ 0x6107, KEY_CHANNELDOWN },     /* CH- */
	{ 0x6108, KEY_2 },
	{ 0x6109, KEY_CHANNELUP },       /* CH+ */
	{ 0x610a, KEY_9 },
	{ 0x610b, KEY_ZOOM },            /* Zoom */
	{ 0x610c, KEY_7 },
	{ 0x610d, KEY_8 },
	{ 0x610e, KEY_VOLUMEUP },        /* Vol+ */
	{ 0x610f, KEY_4 },
	{ 0x6110, KEY_ESC },             /* [back up arrow] */
	{ 0x6111, KEY_0 },
	{ 0x6112, KEY_OK },              /* [enter arrow] */
	{ 0x6113, KEY_VOLUMEDOWN },      /* Vol- */
	{ 0x6114, KEY_RECORD },          /* Rec */
	{ 0x6115, KEY_STOP },            /* Stop */
	{ 0x6116, KEY_PLAY },            /* Play */
	{ 0x6117, KEY_MUTE },            /* Mute */
	{ 0x6118, KEY_UP },
	{ 0x6119, KEY_DOWN },
	{ 0x611a, KEY_LEFT },
	{ 0x611b, KEY_RIGHT },
	{ 0x611c, KEY_RED },
	{ 0x611d, KEY_GREEN },
	{ 0x611e, KEY_YELLOW },
	{ 0x611f, KEY_BLUE },
	{ 0x6143, KEY_POWER },           /* [red power button] */
};

/* TerraTec - 4x7 slim remote */
/* Uses NEC extended 0x02bd. Extended byte removed for compatibility... */
static struct ir_scancode af9015_rc_terratec[] = {
	{ 0x0200, KEY_1 },
	{ 0x0201, KEY_2 },
	{ 0x0202, KEY_3 },
	{ 0x0203, KEY_4 },
	{ 0x0204, KEY_5 },
	{ 0x0205, KEY_6 },
	{ 0x0206, KEY_7 },
	{ 0x0207, KEY_8 },
	{ 0x0208, KEY_9 },
	{ 0x0209, KEY_0 },
	{ 0x020a, KEY_MUTE },
	{ 0x020b, KEY_ZOOM },            /* symbol: PIP or zoom ? */
	{ 0x020e, KEY_VOLUMEDOWN },
	{ 0x020f, KEY_PLAYPAUSE },
	{ 0x0210, KEY_RIGHT },
	{ 0x0211, KEY_LEFT },
	{ 0x0212, KEY_UP },
	{ 0x0213, KEY_DOWN },
	{ 0x0215, KEY_OK },
	{ 0x0216, KEY_STOP },
	{ 0x0217, KEY_CAMERA },          /* snapshot */
	{ 0x0218, KEY_CHANNELUP },
	{ 0x0219, KEY_RECORD },
	{ 0x021a, KEY_CHANNELDOWN },
	{ 0x021c, KEY_ESC },
	{ 0x021f, KEY_VOLUMEUP },
	{ 0x0244, KEY_EPG },
	{ 0x0245, KEY_POWER2 },          /* [red power button] */
};

#endif
