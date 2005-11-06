/*
 *
 * i2c tv tuner chip device driver
 * controls the philips tda8290+75 tuner chip combo.
 */
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/delay.h>
#include <media/tuner.h>

#define I2C_ADDR_TDA8290        0x4b
#define I2C_ADDR_TDA8275        0x61

/* ---------------------------------------------------------------------- */

struct freq_entry {
	u16	freq;
	u8	value;
};

static struct freq_entry band_table[] = {
	{ 0x2DF4, 0x1C },
	{ 0x2574, 0x14 },
	{ 0x22B4, 0x0C },
	{ 0x20D4, 0x0B },
	{ 0x1E74, 0x3B },
	{ 0x1C34, 0x33 },
	{ 0x16F4, 0x5B },
	{ 0x1454, 0x53 },
	{ 0x12D4, 0x52 },
	{ 0x1034, 0x4A },
	{ 0x0EE4, 0x7A },
	{ 0x0D34, 0x72 },
	{ 0x0B54, 0x9A },
	{ 0x0914, 0x91 },
	{ 0x07F4, 0x89 },
	{ 0x0774, 0xB9 },
	{ 0x067B, 0xB1 },
	{ 0x0634, 0xD9 },
	{ 0x05A4, 0xD8 },	// FM radio
	{ 0x0494, 0xD0 },
	{ 0x03BC, 0xC8 },
	{ 0x0394, 0xF8 },	// 57250000 Hz
	{ 0x0000, 0xF0 },	// 0
};

static struct freq_entry div_table[] = {
	{ 0x1C34, 3 },
	{ 0x0D34, 2 },
	{ 0x067B, 1 },
        { 0x0000, 0 },
};

static struct freq_entry agc_table[] = {
	{ 0x22B4, 0x8F },
	{ 0x0B54, 0x9F },
	{ 0x09A4, 0x8F },
	{ 0x0554, 0x9F },
	{ 0x0000, 0xBF },
};

static __u8 get_freq_entry( struct freq_entry* table, __u16 freq)
{
	while(table->freq && table->freq > freq)
		table++;
	return table->value;
}

/* ---------------------------------------------------------------------- */

static unsigned char i2c_enable_bridge[2] = 	{ 0x21, 0xC0 };
static unsigned char i2c_disable_bridge[2] = 	{ 0x21, 0x80 };
static unsigned char i2c_init_tda8275[14] = 	{ 0x00, 0x00, 0x00, 0x00,
						  0xfC, 0x04, 0xA3, 0x3F,
						  0x2A, 0x04, 0xFF, 0x00,
						  0x00, 0x40 };
static unsigned char i2c_set_VS[2] = 		{ 0x30, 0x6F };
static unsigned char i2c_set_GP01_CF[2] = 	{ 0x20, 0x0B };
static unsigned char i2c_tda8290_reset[2] =	{ 0x00, 0x00 };
static unsigned char i2c_tda8290_standby[2] =	{ 0x00, 0x02 };
static unsigned char i2c_gainset_off[2] =	{ 0x28, 0x14 };
static unsigned char i2c_gainset_on[2] =	{ 0x28, 0x54 };
static unsigned char i2c_agc3_00[2] =		{ 0x80, 0x00 };
static unsigned char i2c_agc2_BF[2] =		{ 0x60, 0xBF };
static unsigned char i2c_cb1_D0[2] =		{ 0x30, 0xD0 };
static unsigned char i2c_cb1_D2[2] =		{ 0x30, 0xD2 };
static unsigned char i2c_cb1_56[2] =		{ 0x30, 0x56 };
static unsigned char i2c_cb1_52[2] =		{ 0x30, 0x52 };
static unsigned char i2c_cb1_50[2] =		{ 0x30, 0x50 };
static unsigned char i2c_agc2_7F[2] =		{ 0x60, 0x7F };
static unsigned char i2c_agc3_08[2] =		{ 0x80, 0x08 };

static struct i2c_msg i2c_msg_init[] = {
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_init_tda8275), i2c_init_tda8275 },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_disable_bridge), i2c_disable_bridge },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_set_VS), i2c_set_VS },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_set_GP01_CF), i2c_set_GP01_CF },
};

static struct i2c_msg i2c_msg_prolog[] = {
//	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_easy_mode), i2c_easy_mode },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_gainset_off), i2c_gainset_off },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_tda8290_reset), i2c_tda8290_reset },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_enable_bridge), i2c_enable_bridge },
};

static struct i2c_msg i2c_msg_config[] = {
//	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_set_freq), i2c_set_freq },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_agc3_00), i2c_agc3_00 },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_agc2_BF), i2c_agc2_BF },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_cb1_D2), i2c_cb1_D2 },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_cb1_56), i2c_cb1_56 },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_cb1_52), i2c_cb1_52 },
};

static struct i2c_msg i2c_msg_epilog[] = {
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_cb1_50), i2c_cb1_50 },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_agc2_7F), i2c_agc2_7F },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_agc3_08), i2c_agc3_08 },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_disable_bridge), i2c_disable_bridge },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_gainset_on), i2c_gainset_on },
};

static struct i2c_msg i2c_msg_standby[] = {
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_enable_bridge), i2c_enable_bridge },
	{ I2C_ADDR_TDA8275, 0, ARRAY_SIZE(i2c_cb1_D0), i2c_cb1_D0 },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_disable_bridge), i2c_disable_bridge },
	{ I2C_ADDR_TDA8290, 0, ARRAY_SIZE(i2c_tda8290_standby), i2c_tda8290_standby },
};

static int tda8290_tune(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);
	struct i2c_msg easy_mode =
		{ I2C_ADDR_TDA8290, 0, 2, t->i2c_easy_mode };
	struct i2c_msg set_freq =
		{ I2C_ADDR_TDA8275, 0, 8, t->i2c_set_freq  };

	i2c_transfer(c->adapter, &easy_mode,      1);
	i2c_transfer(c->adapter, i2c_msg_prolog, ARRAY_SIZE(i2c_msg_prolog));

	i2c_transfer(c->adapter, &set_freq,       1);
	i2c_transfer(c->adapter, i2c_msg_config, ARRAY_SIZE(i2c_msg_config));

	msleep(550);
	i2c_transfer(c->adapter, i2c_msg_epilog, ARRAY_SIZE(i2c_msg_epilog));
	return 0;
}

static void set_frequency(struct tuner *t, u16 ifc, unsigned int freq)
{
	u32 N;

	if (t->mode == V4L2_TUNER_RADIO)
		freq = freq / 1000;

	N = (((freq<<3)+ifc)&0x3fffc);

	N = N >> get_freq_entry(div_table, freq);
	t->i2c_set_freq[0] = 0;
	t->i2c_set_freq[1] = (unsigned char)(N>>8);
	t->i2c_set_freq[2] = (unsigned char) N;
	t->i2c_set_freq[3] = 0x40;
	t->i2c_set_freq[4] = 0x52;
	t->i2c_set_freq[5] = get_freq_entry(band_table, freq);
	t->i2c_set_freq[6] = get_freq_entry(agc_table,  freq);
	t->i2c_set_freq[7] = 0x8f;
}

#define V4L2_STD_MN	(V4L2_STD_PAL_M|V4L2_STD_PAL_N|V4L2_STD_PAL_Nc|V4L2_STD_NTSC)
#define V4L2_STD_B	(V4L2_STD_PAL_B|V4L2_STD_PAL_B1|V4L2_STD_SECAM_B)
#define V4L2_STD_GH	(V4L2_STD_PAL_G|V4L2_STD_PAL_H|V4L2_STD_SECAM_G|V4L2_STD_SECAM_H)
#define V4L2_STD_DK	(V4L2_STD_PAL_DK|V4L2_STD_SECAM_DK)

static void set_audio(struct tuner *t)
{
	t->i2c_easy_mode[0] = 0x01;

	if (t->std & V4L2_STD_MN)
		t->i2c_easy_mode[1] = 0x01;
	else if (t->std & V4L2_STD_B)
		t->i2c_easy_mode[1] = 0x02;
	else if (t->std & V4L2_STD_GH)
		t->i2c_easy_mode[1] = 0x04;
	else if (t->std & V4L2_STD_PAL_I)
		t->i2c_easy_mode[1] = 0x08;
	else if (t->std & V4L2_STD_DK)
		t->i2c_easy_mode[1] = 0x10;
	else if (t->std & V4L2_STD_SECAM_L)
		t->i2c_easy_mode[1] = 0x20;
}

static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	set_audio(t);
	set_frequency(t, 864, freq);
	tda8290_tune(c);
}

static void set_radio_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);
	set_frequency(t, 704, freq);
	tda8290_tune(c);
}

static int has_signal(struct i2c_client *c)
{
	unsigned char i2c_get_afc[1] = { 0x1B };
	unsigned char afc = 0;

	i2c_master_send(c, i2c_get_afc, ARRAY_SIZE(i2c_get_afc));
	i2c_master_recv(c, &afc, 1);
	return (afc & 0x80)? 65535:0;
}

static void standby(struct i2c_client *c)
{
	i2c_transfer(c->adapter, i2c_msg_standby, ARRAY_SIZE(i2c_msg_standby));
}

int tda8290_init(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);

	strlcpy(c->name, "tda8290+75", sizeof(c->name));
	tuner_info("tuner: type set to %s\n", c->name);
	t->tv_freq    = set_tv_freq;
	t->radio_freq = set_radio_freq;
	t->has_signal = has_signal;
	t->standby = standby;

	i2c_master_send(c, i2c_enable_bridge, ARRAY_SIZE(i2c_enable_bridge));
	i2c_transfer(c->adapter, i2c_msg_init, ARRAY_SIZE(i2c_msg_init));
	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
