#ifndef quickcam_messenger_h
#define quickcam_messenger_h

#ifndef CONFIG_INPUT
/* if we're not using input we dummy out these functions */
#define qcm_register_input(...)
#define qcm_unregister_input(...)
#define qcm_report_buttonstat(...)
#define qcm_setup_input_int(...) 0
#define qcm_stop_int_data(...)
#define qcm_alloc_int_urb(...) 0
#define qcm_free_int(...)
#endif


#define CHECK_RET(ret, expr) \
	if ((ret = expr) < 0) return ret

/* Control Registers for the STVV6422 ASIC
 * - this define is taken from the qc-usb-messenger code
 */
#define STV_ISO_ENABLE		0x1440
#define ISOC_PACKET_SIZE	1023

/* Chip identification number including revision indicator */
#define CMOS_SENSOR_IDREV	0xE00A

struct rgb {
	u8 b;
	u8 g;
	u8 r;
	u8 b2;
	u8 g2;
	u8 r2;
};

struct bayL0 {
	u8 g;
	u8 r;
};

struct bayL1 {
	u8 b;
	u8 g;
};

struct cam_size {
	u16	width;
	u16	height;
	u8	cmd;
};

static const struct cam_size camera_sizes[] = {
	{ 160, 120, 0xf },
	{ 320, 240, 0x2 },
};

enum frame_sizes {
	SIZE_160X120	= 0,
	SIZE_320X240	= 1,
};

#define MAX_FRAME_SIZE SIZE_320X240

struct qcm {
	u16 colour;
	u16 hue;
	u16 brightness;
	u16 contrast;
	u16 whiteness;

	u8 size;
	int height;
	int width;
	u8 *scratch;
	struct urb *button_urb;
	u8 button_sts;
	u8 button_sts_buf;

#ifdef CONFIG_INPUT
	struct input_dev *input;
	char input_physname[64];
#endif
};

struct regval {
	u16 reg;
	u8 val;
};
/* this table is derived from the
qc-usb-messenger code */
static const struct regval regval_table[] = {
	{ STV_ISO_ENABLE, 0x00 },
	{ 0x1436, 0x00 }, { 0x1432, 0x03 },
	{ 0x143a, 0xF9 }, { 0x0509, 0x38 },
	{ 0x050a, 0x38 }, { 0x050b, 0x38 },
	{ 0x050c, 0x2A }, { 0x050d, 0x01 },
	{ 0x1431, 0x00 }, { 0x1433, 0x34 },
	{ 0x1438, 0x18 }, { 0x1439, 0x00 },
	{ 0x143b, 0x05 }, { 0x143c, 0x00 },
	{ 0x143e, 0x01 }, { 0x143d, 0x00 },
	{ 0x1442, 0xe2 }, { 0x1500, 0xd0 },
	{ 0x1500, 0xd0 }, { 0x1500, 0x50 },
	{ 0x1501, 0xaf }, { 0x1502, 0xc2 },
	{ 0x1503, 0x45 }, { 0x1505, 0x02 },
	{ 0x150e, 0x8e }, { 0x150f, 0x37 },
	{ 0x15c0, 0x00 },
};

static const unsigned char marker[] = { 0x00, 0xff, 0x00, 0xFF };

#endif /* quickcam_messenger_h */
