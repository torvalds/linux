#define TS7800_FPGA_MAGIC 0x00b480
#define FPGAID(_magic, _rev) ((_magic << 8) + _rev)

/*
 * get yer id's from http://ts78xx.digriz.org.uk/
 * do *not* make up your own or 'borrow' any!
 */
enum fpga_ids {
	/* Technologic Systems */
	TS7800_REV_1	= FPGAID(TS7800_FPGA_MAGIC, 0x01),
	TS7800_REV_2	= FPGAID(TS7800_FPGA_MAGIC, 0x02),
	TS7800_REV_3	= FPGAID(TS7800_FPGA_MAGIC, 0x03),
	TS7800_REV_4	= FPGAID(TS7800_FPGA_MAGIC, 0x04),
	TS7800_REV_5	= FPGAID(TS7800_FPGA_MAGIC, 0x05),
	TS7800_REV_6	= FPGAID(TS7800_FPGA_MAGIC, 0x06),
	TS7800_REV_7	= FPGAID(TS7800_FPGA_MAGIC, 0x07),
	TS7800_REV_8	= FPGAID(TS7800_FPGA_MAGIC, 0x08),
	TS7800_REV_9	= FPGAID(TS7800_FPGA_MAGIC, 0x09),

	/* Unaffordable & Expensive */
	UAE_DUMMY	= FPGAID(0xffffff, 0x01),
};

struct fpga_device {
	unsigned		present:1;
	unsigned		init:1;
};

struct fpga_devices {
	/* Technologic Systems */
	struct fpga_device	ts_rtc;
	struct fpga_device	ts_nand;
	struct fpga_device	ts_rng;
};

struct ts78xx_fpga_data {
	unsigned int		id;
	int			state;

	struct fpga_devices	supports;
};
