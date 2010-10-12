/*
 * Copyright 2007-2008 Nouveau Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NOUVEAU_BIOS_H__
#define __NOUVEAU_BIOS_H__

#include "nvreg.h"
#include "nouveau_i2c.h"

#define DCB_MAX_NUM_ENTRIES 16
#define DCB_MAX_NUM_I2C_ENTRIES 16
#define DCB_MAX_NUM_GPIO_ENTRIES 32
#define DCB_MAX_NUM_CONNECTOR_ENTRIES 16

#define DCB_LOC_ON_CHIP 0

struct dcb_i2c_entry {
	uint32_t entry;
	uint8_t port_type;
	uint8_t read, write;
	struct nouveau_i2c_chan *chan;
};

enum dcb_gpio_tag {
	DCB_GPIO_TVDAC0 = 0xc,
	DCB_GPIO_TVDAC1 = 0x2d,
};

struct dcb_gpio_entry {
	enum dcb_gpio_tag tag;
	int line;
	bool invert;
	uint32_t entry;
	uint8_t state_default;
	uint8_t state[2];
};

struct dcb_gpio_table {
	int entries;
	struct dcb_gpio_entry entry[DCB_MAX_NUM_GPIO_ENTRIES];
};

enum dcb_connector_type {
	DCB_CONNECTOR_VGA = 0x00,
	DCB_CONNECTOR_TV_0 = 0x10,
	DCB_CONNECTOR_TV_1 = 0x11,
	DCB_CONNECTOR_TV_3 = 0x13,
	DCB_CONNECTOR_DVI_I = 0x30,
	DCB_CONNECTOR_DVI_D = 0x31,
	DCB_CONNECTOR_LVDS = 0x40,
	DCB_CONNECTOR_DP = 0x46,
	DCB_CONNECTOR_eDP = 0x47,
	DCB_CONNECTOR_HDMI_0 = 0x60,
	DCB_CONNECTOR_HDMI_1 = 0x61,
	DCB_CONNECTOR_NONE = 0xff
};

struct dcb_connector_table_entry {
	uint8_t index;
	uint32_t entry;
	enum dcb_connector_type type;
	uint8_t index2;
	uint8_t gpio_tag;
	void *drm;
};

struct dcb_connector_table {
	int entries;
	struct dcb_connector_table_entry entry[DCB_MAX_NUM_CONNECTOR_ENTRIES];
};

enum dcb_type {
	OUTPUT_ANALOG = 0,
	OUTPUT_TV = 1,
	OUTPUT_TMDS = 2,
	OUTPUT_LVDS = 3,
	OUTPUT_DP = 6,
	OUTPUT_EOL = 14, /* DCB 4.0+, appears to be end-of-list */
	OUTPUT_ANY = -1
};

struct dcb_entry {
	int index;	/* may not be raw dcb index if merging has happened */
	enum dcb_type type;
	uint8_t i2c_index;
	uint8_t heads;
	uint8_t connector;
	uint8_t bus;
	uint8_t location;
	uint8_t or;
	bool duallink_possible;
	union {
		struct sor_conf {
			int link;
		} sorconf;
		struct {
			int maxfreq;
		} crtconf;
		struct {
			struct sor_conf sor;
			bool use_straps_for_mode;
			bool use_acpi_for_edid;
			bool use_power_scripts;
		} lvdsconf;
		struct {
			bool has_component_output;
		} tvconf;
		struct {
			struct sor_conf sor;
			int link_nr;
			int link_bw;
		} dpconf;
		struct {
			struct sor_conf sor;
			int slave_addr;
		} tmdsconf;
	};
	bool i2c_upper_default;
};

struct dcb_table {
	uint8_t version;

	int entries;
	struct dcb_entry entry[DCB_MAX_NUM_ENTRIES];

	uint8_t *i2c_table;
	uint8_t i2c_default_indices;
	struct dcb_i2c_entry i2c[DCB_MAX_NUM_I2C_ENTRIES];

	uint16_t gpio_table_ptr;
	struct dcb_gpio_table gpio;
	uint16_t connector_table_ptr;
	struct dcb_connector_table connector;
};

enum nouveau_or {
	OUTPUT_A = (1 << 0),
	OUTPUT_B = (1 << 1),
	OUTPUT_C = (1 << 2)
};

enum LVDS_script {
	/* Order *does* matter here */
	LVDS_INIT = 1,
	LVDS_RESET,
	LVDS_BACKLIGHT_ON,
	LVDS_BACKLIGHT_OFF,
	LVDS_PANEL_ON,
	LVDS_PANEL_OFF
};

/* changing these requires matching changes to reg tables in nv_get_clock */
#define MAX_PLL_TYPES	4
enum pll_types {
	NVPLL,
	MPLL,
	VPLL1,
	VPLL2
};

struct pll_lims {
	struct {
		int minfreq;
		int maxfreq;
		int min_inputfreq;
		int max_inputfreq;

		uint8_t min_m;
		uint8_t max_m;
		uint8_t min_n;
		uint8_t max_n;
	} vco1, vco2;

	uint8_t max_log2p;
	/*
	 * for most pre nv50 cards setting a log2P of 7 (the common max_log2p
	 * value) is no different to 6 (at least for vplls) so allowing the MNP
	 * calc to use 7 causes the generated clock to be out by a factor of 2.
	 * however, max_log2p cannot be fixed-up during parsing as the
	 * unmodified max_log2p value is still needed for setting mplls, hence
	 * an additional max_usable_log2p member
	 */
	uint8_t max_usable_log2p;
	uint8_t log2p_bias;

	uint8_t min_p;
	uint8_t max_p;

	int refclk;
};

struct nvbios {
	struct drm_device *dev;

	uint8_t chip_version;

	uint32_t dactestval;
	uint32_t tvdactestval;
	uint8_t digital_min_front_porch;
	bool fp_no_ddc;

	struct mutex lock;

	uint8_t data[NV_PROM_SIZE];
	unsigned int length;
	bool execute;

	uint8_t major_version;
	uint8_t feature_byte;
	bool is_mobile;

	uint32_t fmaxvco, fminvco;

	bool old_style_init;
	uint16_t init_script_tbls_ptr;
	uint16_t extra_init_script_tbl_ptr;
	uint16_t macro_index_tbl_ptr;
	uint16_t macro_tbl_ptr;
	uint16_t condition_tbl_ptr;
	uint16_t io_condition_tbl_ptr;
	uint16_t io_flag_condition_tbl_ptr;
	uint16_t init_function_tbl_ptr;

	uint16_t pll_limit_tbl_ptr;
	uint16_t ram_restrict_tbl_ptr;
	uint8_t ram_restrict_group_count;

	uint16_t some_script_ptr; /* BIT I + 14 */
	uint16_t init96_tbl_ptr; /* BIT I + 16 */

	struct dcb_table dcb;

	struct {
		int crtchead;
	} state;

	struct {
		struct dcb_entry *output;
		uint16_t script_table_ptr;
		uint16_t dp_table_ptr;
	} display;

	struct {
		uint16_t fptablepointer;	/* also used by tmds */
		uint16_t fpxlatetableptr;
		int xlatwidth;
		uint16_t lvdsmanufacturerpointer;
		uint16_t fpxlatemanufacturertableptr;
		uint16_t mode_ptr;
		uint16_t xlated_entry;
		bool power_off_for_reset;
		bool reset_after_pclk_change;
		bool dual_link;
		bool link_c_increment;
		bool if_is_24bit;
		int duallink_transition_clk;
		uint8_t strapless_is_24bit;
		uint8_t *edid;

		/* will need resetting after suspend */
		int last_script_invoc;
		bool lvds_init_run;
	} fp;

	struct {
		uint16_t output0_script_ptr;
		uint16_t output1_script_ptr;
	} tmds;

	struct {
		uint16_t mem_init_tbl_ptr;
		uint16_t sdr_seq_tbl_ptr;
		uint16_t ddr_seq_tbl_ptr;

		struct {
			uint8_t crt, tv, panel;
		} i2c_indices;

		uint16_t lvds_single_a_script_ptr;
	} legacy;
};

#endif
