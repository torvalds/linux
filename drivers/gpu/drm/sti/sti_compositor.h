/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_COMPOSITOR_H_
#define _STI_COMPOSITOR_H_

#include <linux/clk.h>
#include <linux/kernel.h>

#include "sti_layer.h"
#include "sti_mixer.h"

#define WAIT_NEXT_VSYNC_MS      50 /*ms*/

#define STI_MAX_LAYER 8
#define STI_MAX_MIXER 2

enum sti_compositor_subdev_type {
	STI_MIXER_MAIN_SUBDEV,
	STI_MIXER_AUX_SUBDEV,
	STI_GPD_SUBDEV,
	STI_VID_SUBDEV,
	STI_CURSOR_SUBDEV,
};

struct sti_compositor_subdev_descriptor {
	enum sti_compositor_subdev_type type;
	int id;
	unsigned int offset;
};

/**
 * STI Compositor data structure
 *
 * @nb_subdev: number of subdevices supported by the compositor
 * @subdev_desc: subdev list description
 */
#define MAX_SUBDEV 9
struct sti_compositor_data {
	unsigned int nb_subdev;
	struct sti_compositor_subdev_descriptor subdev_desc[MAX_SUBDEV];
};

/**
 * STI Compositor structure
 *
 * @dev: driver device
 * @regs: registers (main)
 * @data: device data
 * @clk_compo_main: clock for main compo
 * @clk_compo_aux: clock for aux compo
 * @clk_pix_main: pixel clock for main path
 * @clk_pix_aux: pixel clock for aux path
 * @rst_main: reset control of the main path
 * @rst_aux: reset control of the aux path
 * @mixer: array of mixers
 * @vtg_main: vtg for main data path
 * @vtg_aux: vtg for auxillary data path
 * @layer: array of layers
 * @nb_mixers: number of mixers for this compositor
 * @nb_layers: number of layers (GDP,VID,...) for this compositor
 * @enable: true if compositor is enable else false
 * @vtg_vblank_nb: callback for VTG VSYNC notification
 */
struct sti_compositor {
	struct device *dev;
	void __iomem *regs;
	struct sti_compositor_data data;
	struct clk *clk_compo_main;
	struct clk *clk_compo_aux;
	struct clk *clk_pix_main;
	struct clk *clk_pix_aux;
	struct reset_control *rst_main;
	struct reset_control *rst_aux;
	struct sti_mixer *mixer[STI_MAX_MIXER];
	struct sti_vtg *vtg_main;
	struct sti_vtg *vtg_aux;
	struct sti_layer *layer[STI_MAX_LAYER];
	int nb_mixers;
	int nb_layers;
	bool enable;
	struct notifier_block vtg_vblank_nb;
};

#endif
