/*
 */

#ifndef MSP3400_H
#define MSP3400_H

/* ---------------------------------------------------------------------- */

#define msp_err(fmt, arg...) \
	printk(KERN_ERR "%s %d-%04x: " fmt, client->driver->driver.name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg)
#define msp_warn(fmt, arg...) \
	printk(KERN_WARNING "%s %d-%04x: " fmt, client->driver->driver.name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg)
#define msp_info(fmt, arg...) \
	printk(KERN_INFO "%s %d-%04x: " fmt, client->driver->driver.name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg)

/* level 1 debug. */
#define msp_dbg1(fmt, arg...) \
	do { \
		if (debug) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->driver.name, \
			       i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

/* level 2 debug. */
#define msp_dbg2(fmt, arg...) \
	do { \
		if (debug >= 2) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->name, \
				i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

/* level 3 debug. Use with care. */
#define msp_dbg3(fmt, arg...) \
	do { \
		if (debug >= 16) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->name, \
				i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

struct msp_matrix {
  int input;
  int output;
};

/* ioctl for MSP_SET_MATRIX will have to be registered */
#define MSP_SET_MATRIX     _IOW('m',17,struct msp_matrix)

/* This macro is allowed for *constants* only, gcc must calculate it
   at compile time.  Remember -- no floats in kernel mode */
#define MSP_CARRIER(freq) ((int)((float)(freq / 18.432) * (1 << 24)))

#define MSP_MODE_AM_DETECT   0
#define MSP_MODE_FM_RADIO    2
#define MSP_MODE_FM_TERRA    3
#define MSP_MODE_FM_SAT      4
#define MSP_MODE_FM_NICAM1   5
#define MSP_MODE_FM_NICAM2   6
#define MSP_MODE_AM_NICAM    7
#define MSP_MODE_BTSC        8
#define MSP_MODE_EXTERN      9

#define SCART_MASK    0
#define SCART_IN1     1
#define SCART_IN2     2
#define SCART_IN1_DA  3
#define SCART_IN2_DA  4
#define SCART_IN3     5
#define SCART_IN4     6
#define SCART_MONO    7
#define SCART_MUTE    8

#define SCART_DSP_IN  0
#define SCART1_OUT    1
#define SCART2_OUT    2

#define OPMODE_AUTO       -1
#define OPMODE_MANUAL      0
#define OPMODE_AUTODETECT  1   /* use autodetect (>= msp3410 only) */
#define OPMODE_AUTOSELECT  2   /* use autodetect & autoselect (>= msp34xxG)   */

/* module parameters */
extern int debug;
extern int once;
extern int amsound;
extern int standard;
extern int dolby;
extern int stereo_threshold;

struct msp_state {
	int rev1, rev2;

	int opmode;
	int mode;
	int norm;
	int stereo;
	int nicam_on;
	int acb;
	int in_scart;
	int i2s_mode;
	int main, second;	/* sound carrier */
	int input;
	int source;             /* see msp34xxg_set_source */

	/* v4l2 */
	int audmode;
	int rxsubchans;

	int muted;
	int volume, balance;
	int bass, treble;

	/* thread */
	struct task_struct   *kthread;
	wait_queue_head_t    wq;
	int                  restart:1;
	int                  watch_stereo:1;
};

#define VIDEO_MODE_RADIO 16      /* norm magic for radio mode */

#define HAVE_NICAM(state)   (((state->rev2 >> 8) & 0xff) != 0)
#define HAVE_RADIO(state)   ((state->rev1 & 0x0f) >= 'G'-'@')

/* msp3400-driver.c */
int msp_write_dem(struct i2c_client *client, int addr, int val);
int msp_write_dsp(struct i2c_client *client, int addr, int val);
int msp_read_dem(struct i2c_client *client, int addr);
int msp_read_dsp(struct i2c_client *client, int addr);
int msp_reset(struct i2c_client *client);
void msp_set_scart(struct i2c_client *client, int in, int out);
void msp_set_mute(struct i2c_client *client);
void msp_set_audio(struct i2c_client *client);
int msp_modus(struct i2c_client *client, int norm);
int msp_standard(int norm);
int msp_sleep(struct msp_state *state, int timeout);

/* msp3400-kthreads.c */
const char *msp_standard_mode_name(int mode);
void msp3400c_setcarrier(struct i2c_client *client, int cdo1, int cdo2);
void msp3400c_setmode(struct i2c_client *client, int type);
void msp3400c_setstereo(struct i2c_client *client, int mode);
int autodetect_stereo(struct i2c_client *client);
int msp3400c_thread(void *data);
int msp3410d_thread(void *data);
int msp34xxg_thread(void *data);
void msp34xxg_detect_stereo(struct i2c_client *client);
void msp34xxg_set_audmode(struct i2c_client *client, int audmode);

#endif /* MSP3400_H */
