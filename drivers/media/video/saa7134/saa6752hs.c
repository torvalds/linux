#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/init.h>
#include <linux/crc32.h>

#include <media/id.h>

#define MPEG_VIDEO_TARGET_BITRATE_MAX  27000
#define MPEG_VIDEO_MAX_BITRATE_MAX     27000
#define MPEG_TOTAL_TARGET_BITRATE_MAX  27000
#define MPEG_PID_MAX ((1 << 14) - 1)

/* Addresses to scan */
static unsigned short normal_i2c[] = {0x20, I2C_CLIENT_END};
I2C_CLIENT_INSMOD;

MODULE_DESCRIPTION("device driver for saa6752hs MPEG2 encoder");
MODULE_AUTHOR("Andrew de Quincey");
MODULE_LICENSE("GPL");

static struct i2c_driver driver;
static struct i2c_client client_template;

enum saa6752hs_videoformat {
	SAA6752HS_VF_D1 = 0,    /* standard D1 video format: 720x576 */
	SAA6752HS_VF_2_3_D1 = 1,/* 2/3D1 video format: 480x576 */
	SAA6752HS_VF_1_2_D1 = 2,/* 1/2D1 video format: 352x576 */
	SAA6752HS_VF_SIF = 3,   /* SIF video format: 352x288 */
	SAA6752HS_VF_UNKNOWN,
};

static const struct v4l2_format v4l2_format_table[] =
{
	[SAA6752HS_VF_D1] =
		{ .fmt = { .pix = { .width = 720, .height = 576 }}},
	[SAA6752HS_VF_2_3_D1] =
		{ .fmt = { .pix = { .width = 480, .height = 576 }}},
	[SAA6752HS_VF_1_2_D1] =
		{ .fmt = { .pix = { .width = 352, .height = 576 }}},
	[SAA6752HS_VF_SIF] =
		{ .fmt = { .pix = { .width = 352, .height = 288 }}},
	[SAA6752HS_VF_UNKNOWN] =
		{ .fmt = { .pix = { .width = 0, .height = 0}}},
};

struct saa6752hs_state {
	struct i2c_client             client;
	struct v4l2_mpeg_compression  params;
	enum saa6752hs_videoformat    video_format;
};

enum saa6752hs_command {
	SAA6752HS_COMMAND_RESET = 0,
    	SAA6752HS_COMMAND_STOP = 1,
    	SAA6752HS_COMMAND_START = 2,
    	SAA6752HS_COMMAND_PAUSE = 3,
    	SAA6752HS_COMMAND_RECONFIGURE = 4,
    	SAA6752HS_COMMAND_SLEEP = 5,
	SAA6752HS_COMMAND_RECONFIGURE_FORCE = 6,

	SAA6752HS_COMMAND_MAX
};

/* ---------------------------------------------------------------------- */

static u8 PAT[] = {
	0xc2, // i2c register
	0x00, // table number for encoder

	0x47, // sync
	0x40, 0x00, // transport_error_indicator(0), payload_unit_start(1), transport_priority(0), pid(0)
	0x10, // transport_scrambling_control(00), adaptation_field_control(01), continuity_counter(0)

	0x00, // PSI pointer to start of table

	0x00, // tid(0)
	0xb0, 0x0d, // section_syntax_indicator(1), section_length(13)

	0x00, 0x01, // transport_stream_id(1)

	0xc1, // version_number(0), current_next_indicator(1)

	0x00, 0x00, // section_number(0), last_section_number(0)

	0x00, 0x01, // program_number(1)

	0xe0, 0x00, // PMT PID

	0x00, 0x00, 0x00, 0x00 // CRC32
};

static u8 PMT[] = {
	0xc2, // i2c register
	0x01, // table number for encoder

	0x47, // sync
	0x40, 0x00, // transport_error_indicator(0), payload_unit_start(1), transport_priority(0), pid
	0x10, // transport_scrambling_control(00), adaptation_field_control(01), continuity_counter(0)

	0x00, // PSI pointer to start of table

	0x02, // tid(2)
	0xb0, 0x17, // section_syntax_indicator(1), section_length(23)

	0x00, 0x01, // program_number(1)

	0xc1, // version_number(0), current_next_indicator(1)

	0x00, 0x00, // section_number(0), last_section_number(0)

	0xe0, 0x00, // PCR_PID

	0xf0, 0x00, // program_info_length(0)

	0x02, 0xe0, 0x00, 0xf0, 0x00, // video stream type(2), pid
	0x04, 0xe0, 0x00, 0xf0, 0x00, // audio stream type(4), pid

	0x00, 0x00, 0x00, 0x00 // CRC32
};

static struct v4l2_mpeg_compression param_defaults =
{
	.st_type         = V4L2_MPEG_TS_2,
	.st_bitrate      = {
		.mode    = V4L2_BITRATE_CBR,
		.target  = 7000,
	},

	.ts_pid_pmt      = 16,
	.ts_pid_video    = 260,
	.ts_pid_audio    = 256,
	.ts_pid_pcr      = 259,

	.vi_type         = V4L2_MPEG_VI_2,
	.vi_aspect_ratio = V4L2_MPEG_ASPECT_4_3,
	.vi_bitrate      = {
		.mode    = V4L2_BITRATE_VBR,
		.target  = 4000,
		.max     = 6000,
	},

	.au_type         = V4L2_MPEG_AU_2_II,
	.au_bitrate      = {
		.mode    = V4L2_BITRATE_CBR,
		.target  = 256,
	},

};

/* ---------------------------------------------------------------------- */

static int saa6752hs_chip_command(struct i2c_client* client,
				  enum saa6752hs_command command)
{
	unsigned char buf[3];
	unsigned long timeout;
	int status = 0;

	// execute the command
	switch(command) {
  	case SAA6752HS_COMMAND_RESET:
  		buf[0] = 0x00;
		break;

	case SAA6752HS_COMMAND_STOP:
	  	buf[0] = 0x03;
		break;

	case SAA6752HS_COMMAND_START:
  		buf[0] = 0x02;
		break;

	case SAA6752HS_COMMAND_PAUSE:
  		buf[0] = 0x04;
		break;

	case SAA6752HS_COMMAND_RECONFIGURE:
		buf[0] = 0x05;
		break;

  	case SAA6752HS_COMMAND_SLEEP:
  		buf[0] = 0x06;
		break;

  	case SAA6752HS_COMMAND_RECONFIGURE_FORCE:
		buf[0] = 0x07;
		break;

	default:
		return -EINVAL;
	}

  	// set it and wait for it to be so
	i2c_master_send(client, buf, 1);
	timeout = jiffies + HZ * 3;
	for (;;) {
		// get the current status
		buf[0] = 0x10;
	  	i2c_master_send(client, buf, 1);
		i2c_master_recv(client, buf, 1);

		if (!(buf[0] & 0x20))
			break;
		if (time_after(jiffies,timeout)) {
			status = -ETIMEDOUT;
			break;
		}

		// wait a bit
		msleep(10);
	}

	// delay a bit to let encoder settle
	msleep(50);

	// done
  	return status;
}


static int saa6752hs_set_bitrate(struct i2c_client* client,
				 struct v4l2_mpeg_compression* params)
{
  	u8 buf[3];

	// set the bitrate mode
	buf[0] = 0x71;
	buf[1] = (params->vi_bitrate.mode == V4L2_BITRATE_VBR) ? 0 : 1;
	i2c_master_send(client, buf, 2);

	// set the video bitrate
	if (params->vi_bitrate.mode == V4L2_BITRATE_VBR) {
		// set the target bitrate
		buf[0] = 0x80;
	    	buf[1] = params->vi_bitrate.target >> 8;
	  	buf[2] = params->vi_bitrate.target & 0xff;
		i2c_master_send(client, buf, 3);

		// set the max bitrate
		buf[0] = 0x81;
	    	buf[1] = params->vi_bitrate.max >> 8;
	  	buf[2] = params->vi_bitrate.max & 0xff;
		i2c_master_send(client, buf, 3);
	} else {
		// set the target bitrate (no max bitrate for CBR)
  		buf[0] = 0x81;
	    	buf[1] = params->vi_bitrate.target >> 8;
	  	buf[2] = params->vi_bitrate.target & 0xff;
		i2c_master_send(client, buf, 3);
	}

	// set the audio bitrate
 	buf[0] = 0x94;
	buf[1] = (256 == params->au_bitrate.target) ? 0 : 1;
	i2c_master_send(client, buf, 2);

	// set the total bitrate
	buf[0] = 0xb1;
  	buf[1] = params->st_bitrate.target >> 8;
  	buf[2] = params->st_bitrate.target & 0xff;
	i2c_master_send(client, buf, 3);

	// return success
	return 0;
}

static void saa6752hs_set_subsampling(struct i2c_client* client,
				      struct v4l2_format* f)
{
	struct saa6752hs_state *h = i2c_get_clientdata(client);
	int dist_352, dist_480, dist_720;

	/*
	  FIXME: translate and round width/height into EMPRESS
	  subsample type:

	  type   |   PAL   |  NTSC
	  ---------------------------
	  SIF    | 352x288 | 352x240
	  1/2 D1 | 352x576 | 352x480
	  2/3 D1 | 480x576 | 480x480
	  D1     | 720x576 | 720x480
	*/

	dist_352 = abs(f->fmt.pix.width - 352);
	dist_480 = abs(f->fmt.pix.width - 480);
	dist_720 = abs(f->fmt.pix.width - 720);
	if (dist_720 < dist_480) {
		f->fmt.pix.width = 720;
		f->fmt.pix.height = 576;
		h->video_format = SAA6752HS_VF_D1;
	}
	else if (dist_480 < dist_352) {
		f->fmt.pix.width = 480;
		f->fmt.pix.height = 576;
		h->video_format = SAA6752HS_VF_2_3_D1;
	}
	else {
		f->fmt.pix.width = 352;
		if (abs(f->fmt.pix.height - 576) <
		    abs(f->fmt.pix.height - 288)) {
			f->fmt.pix.height = 576;
			h->video_format = SAA6752HS_VF_1_2_D1;
		}
		else {
			f->fmt.pix.height = 288;
			h->video_format = SAA6752HS_VF_SIF;
		}
	}
}


static void saa6752hs_set_params(struct i2c_client* client,
				 struct v4l2_mpeg_compression* params)
{
	struct saa6752hs_state *h = i2c_get_clientdata(client);

	/* check PIDs */
	if (params->ts_pid_pmt <= MPEG_PID_MAX)
		h->params.ts_pid_pmt = params->ts_pid_pmt;
	if (params->ts_pid_pcr <= MPEG_PID_MAX)
		h->params.ts_pid_pcr = params->ts_pid_pcr;
	if (params->ts_pid_video <= MPEG_PID_MAX)
		h->params.ts_pid_video = params->ts_pid_video;
	if (params->ts_pid_audio <= MPEG_PID_MAX)
		h->params.ts_pid_audio = params->ts_pid_audio;

	/* check bitrate parameters */
	if ((params->vi_bitrate.mode == V4L2_BITRATE_CBR) ||
	    (params->vi_bitrate.mode == V4L2_BITRATE_VBR))
		h->params.vi_bitrate.mode = params->vi_bitrate.mode;
	if (params->vi_bitrate.mode != V4L2_BITRATE_NONE)
		h->params.st_bitrate.target = params->st_bitrate.target;
	if (params->vi_bitrate.mode != V4L2_BITRATE_NONE)
		h->params.vi_bitrate.target = params->vi_bitrate.target;
	if (params->vi_bitrate.mode == V4L2_BITRATE_VBR)
		h->params.vi_bitrate.max = params->vi_bitrate.max;
	if (params->au_bitrate.mode != V4L2_BITRATE_NONE)
		h->params.au_bitrate.target = params->au_bitrate.target;

	/* aspect ratio */
	if (params->vi_aspect_ratio == V4L2_MPEG_ASPECT_4_3 ||
	    params->vi_aspect_ratio == V4L2_MPEG_ASPECT_16_9)
		h->params.vi_aspect_ratio = params->vi_aspect_ratio;

	/* range checks */
	if (h->params.st_bitrate.target > MPEG_TOTAL_TARGET_BITRATE_MAX)
		h->params.st_bitrate.target = MPEG_TOTAL_TARGET_BITRATE_MAX;
	if (h->params.vi_bitrate.target > MPEG_VIDEO_TARGET_BITRATE_MAX)
		h->params.vi_bitrate.target = MPEG_VIDEO_TARGET_BITRATE_MAX;
	if (h->params.vi_bitrate.max > MPEG_VIDEO_MAX_BITRATE_MAX)
		h->params.vi_bitrate.max = MPEG_VIDEO_MAX_BITRATE_MAX;
	if (h->params.au_bitrate.target <= 256)
		h->params.au_bitrate.target = 256;
	else
		h->params.au_bitrate.target = 384;
}

static int saa6752hs_init(struct i2c_client* client)
{
	unsigned char buf[9], buf2[4];
	struct saa6752hs_state *h;
	u32 crc;
	unsigned char localPAT[256];
	unsigned char localPMT[256];

	h = i2c_get_clientdata(client);

	// Set video format - must be done first as it resets other settings
	buf[0] = 0x41;
	buf[1] = h->video_format;
	i2c_master_send(client, buf, 2);

        // set bitrate
        saa6752hs_set_bitrate(client, &h->params);

	// Set GOP structure {3, 13}
	buf[0] = 0x72;
	buf[1] = 0x03;
	buf[2] = 0x0D;
	i2c_master_send(client,buf,3);

    	// Set minimum Q-scale {4}
	buf[0] = 0x82;
	buf[1] = 0x04;
	i2c_master_send(client,buf,2);

    	// Set maximum Q-scale {12}
	buf[0] = 0x83;
	buf[1] = 0x0C;
	i2c_master_send(client,buf,2);

    	// Set Output Protocol
	buf[0] = 0xD0;
	buf[1] = 0x81;
	i2c_master_send(client,buf,2);

    	// Set video output stream format {TS}
	buf[0] = 0xB0;
	buf[1] = 0x05;
	i2c_master_send(client,buf,2);

	/* compute PAT */
	memcpy(localPAT, PAT, sizeof(PAT));
	localPAT[17] = 0xe0 | ((h->params.ts_pid_pmt >> 8) & 0x0f);
	localPAT[18] = h->params.ts_pid_pmt & 0xff;
	crc = crc32_be(~0, &localPAT[7], sizeof(PAT) - 7 - 4);
	localPAT[sizeof(PAT) - 4] = (crc >> 24) & 0xFF;
	localPAT[sizeof(PAT) - 3] = (crc >> 16) & 0xFF;
	localPAT[sizeof(PAT) - 2] = (crc >> 8) & 0xFF;
	localPAT[sizeof(PAT) - 1] = crc & 0xFF;

	/* compute PMT */
      	memcpy(localPMT, PMT, sizeof(PMT));
   	localPMT[3] = 0x40 | ((h->params.ts_pid_pmt >> 8) & 0x0f);
   	localPMT[4] = h->params.ts_pid_pmt & 0xff;
	localPMT[15] = 0xE0 | ((h->params.ts_pid_pcr >> 8) & 0x0F);
	localPMT[16] = h->params.ts_pid_pcr & 0xFF;
	localPMT[20] = 0xE0 | ((h->params.ts_pid_video >> 8) & 0x0F);
	localPMT[21] = h->params.ts_pid_video & 0xFF;
	localPMT[25] = 0xE0 | ((h->params.ts_pid_audio >> 8) & 0x0F);
	localPMT[26] = h->params.ts_pid_audio & 0xFF;
	crc = crc32_be(~0, &localPMT[7], sizeof(PMT) - 7 - 4);
	localPMT[sizeof(PMT) - 4] = (crc >> 24) & 0xFF;
	localPMT[sizeof(PMT) - 3] = (crc >> 16) & 0xFF;
	localPMT[sizeof(PMT) - 2] = (crc >> 8) & 0xFF;
	localPMT[sizeof(PMT) - 1] = crc & 0xFF;

    	// Set Audio PID
	buf[0] = 0xC1;
	buf[1] = (h->params.ts_pid_audio >> 8) & 0xFF;
	buf[2] = h->params.ts_pid_audio & 0xFF;
	i2c_master_send(client,buf,3);

	// Set Video PID
	buf[0] = 0xC0;
	buf[1] = (h->params.ts_pid_video >> 8) & 0xFF;
	buf[2] = h->params.ts_pid_video & 0xFF;
	i2c_master_send(client,buf,3);

 	// Set PCR PID
	buf[0] = 0xC4;
	buf[1] = (h->params.ts_pid_pcr >> 8) & 0xFF;
	buf[2] = h->params.ts_pid_pcr & 0xFF;
	i2c_master_send(client,buf,3);

	// Send SI tables
	i2c_master_send(client,localPAT,sizeof(PAT));
	i2c_master_send(client,localPMT,sizeof(PMT));

	// mute then unmute audio. This removes buzzing artefacts
	buf[0] = 0xa4;
	buf[1] = 1;
	i2c_master_send(client, buf, 2);
  	buf[1] = 0;
	i2c_master_send(client, buf, 2);

	// start it going
	saa6752hs_chip_command(client, SAA6752HS_COMMAND_START);

	// readout current state
	buf[0] = 0xE1;
	buf[1] = 0xA7;
	buf[2] = 0xFE;
	buf[3] = 0x82;
	buf[4] = 0xB0;
	i2c_master_send(client, buf, 5);
	i2c_master_recv(client, buf2, 4);

	// change aspect ratio
	buf[0] = 0xE0;
	buf[1] = 0xA7;
	buf[2] = 0xFE;
	buf[3] = 0x82;
	buf[4] = 0xB0;
	buf[5] = buf2[0];
	switch(h->params.vi_aspect_ratio) {
	case V4L2_MPEG_ASPECT_16_9:
		buf[6] = buf2[1] | 0x40;
		break;
	case V4L2_MPEG_ASPECT_4_3:
	default:
		buf[6] = buf2[1] & 0xBF;
		break;
		break;
	}
	buf[7] = buf2[2];
	buf[8] = buf2[3];
	i2c_master_send(client, buf, 9);

   	// return success
	return 0;
}

static int saa6752hs_attach(struct i2c_adapter *adap, int addr, int kind)
{
	struct saa6752hs_state *h;

        printk("saa6752hs: chip found @ 0x%x\n", addr<<1);

        if (NULL == (h = kmalloc(sizeof(*h), GFP_KERNEL)))
                return -ENOMEM;
	memset(h,0,sizeof(*h));
	h->client = client_template;
	h->params = param_defaults;
	h->client.adapter = adap;
	h->client.addr = addr;

	i2c_set_clientdata(&h->client, h);
        i2c_attach_client(&h->client);
	return 0;
}

static int saa6752hs_probe(struct i2c_adapter *adap)
{
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, saa6752hs_attach);
	return 0;
}

static int saa6752hs_detach(struct i2c_client *client)
{
	struct saa6752hs_state *h;

	h = i2c_get_clientdata(client);
	i2c_detach_client(client);
	kfree(h);
	return 0;
}

static int
saa6752hs_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct saa6752hs_state *h = i2c_get_clientdata(client);
	struct v4l2_mpeg_compression *params = arg;
	int err = 0;

        switch (cmd) {
	case VIDIOC_S_MPEGCOMP:
		if (NULL == params) {
			/* apply settings and start encoder */
			saa6752hs_init(client);
			break;
		}
		saa6752hs_set_params(client, params);
		/* fall through */
	case VIDIOC_G_MPEGCOMP:
		*params = h->params;
		break;
	case VIDIOC_G_FMT:
	{
           struct v4l2_format *f = arg;

	   if (h->video_format == SAA6752HS_VF_UNKNOWN)
		   h->video_format = SAA6752HS_VF_D1;
	   f->fmt.pix.width =
		   v4l2_format_table[h->video_format].fmt.pix.width;
	   f->fmt.pix.height =
		   v4l2_format_table[h->video_format].fmt.pix.height;
	   break ;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;

		saa6752hs_set_subsampling(client, f);
		break;
	}
	default:
		/* nothing */
		break;
	}

	return err;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.owner          = THIS_MODULE,
        .name           = "i2c saa6752hs MPEG encoder",
        .id             = I2C_DRIVERID_SAA6752HS,
        .flags          = I2C_DF_NOTIFY,
        .attach_adapter = saa6752hs_probe,
        .detach_client  = saa6752hs_detach,
        .command        = saa6752hs_command,
};

static struct i2c_client client_template =
{
	.name       = "saa6752hs",
	.flags      = I2C_CLIENT_ALLOW_USE,
        .driver     = &driver,
};

static int __init saa6752hs_init_module(void)
{
	return i2c_add_driver(&driver);
}

static void __exit saa6752hs_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(saa6752hs_init_module);
module_exit(saa6752hs_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
