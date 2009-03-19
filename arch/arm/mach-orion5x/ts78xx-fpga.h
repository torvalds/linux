#define FPGAID(_magic, _rev) ((_magic << 8) + _rev)

/*
 * get yer id's from http://ts78xx.digriz.org.uk/
 * do *not* make up your own or 'borrow' any!
 */
enum fpga_ids {
	/* Technologic Systems */
	TS7800_REV_B2 = FPGAID(0x00b480, 0x02),
	TS7800_REV_B3 = FPGAID(0x00b480, 0x03),
};

struct fpga_device {
	unsigned		present:1;
	unsigned		init:1;
};

struct fpga_devices {
	/* Technologic Systems */
	struct fpga_device 	ts_rtc;
	struct fpga_device 	ts_nand;
};

struct ts78xx_fpga_data {
	unsigned int		id;
	int			state;

	struct fpga_devices	supports;
};
