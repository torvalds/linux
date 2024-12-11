// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#include "ia_css_rmgr.h"

int ia_css_rmgr_init(void)
{
	int err = 0;

	err = ia_css_rmgr_init_vbuf(vbuf_ref);
	if (!err)
		err = ia_css_rmgr_init_vbuf(vbuf_write);
	if (!err)
		err = ia_css_rmgr_init_vbuf(hmm_buffer_pool);
	if (err)
		ia_css_rmgr_uninit();
	return err;
}

/*
 * @brief Uninitialize resource pool (host)
 */
void ia_css_rmgr_uninit(void)
{
	ia_css_rmgr_uninit_vbuf(hmm_buffer_pool);
	ia_css_rmgr_uninit_vbuf(vbuf_write);
	ia_css_rmgr_uninit_vbuf(vbuf_ref);
}
