/* SPDX-License-Identifier: GPL-2.0+ */

#include <linux/firmware.h>

#define	MAX_STR	256

enum fmt_image {
	f_bit,	/* only bitstream is supported */
	f_rbt,
	f_bin,
	f_mcs,
	f_hex,
};

enum mdownload {
	m_systemmap,	/* only system map is supported */
	m_serial,
	m_jtag,
};

/*
 * xilinx fpgaimage information
 * NOTE: use MAX_STR instead of dynamic alloc for simplicity
 */
struct fpgaimage {
	enum fmt_image	fmt_img;
	enum mdownload	dmethod;

	const struct	firmware	*fw_entry;

	/*
	 * the following can be read from bitstream,
	 * but other image format should have as well
	 */
	char	filename[MAX_STR];
	char	part[MAX_STR];
	char	date[MAX_STR];
	char	time[MAX_STR];
	int	lendata;
	u8	*fpgadata;
};
