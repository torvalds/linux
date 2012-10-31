/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "disp_video.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_scaler.h"
#include "disp_de.h"

frame_para_t g_video[2][4];

#ifdef CONFIG_ARCH_SUN4I
static __s32 video_enhancement_start(__u32 sel, __u32 id)
{
	__u32 scaleuprate;
	__u32 scaler_index;
	__u32 gamma_tab[256] = {
		0x00000000, 0x00010101, 0x00020202, 0x00030303, 0x00040404, 0x00050505, 0x00060606, 0x00070707,
		0x00080808, 0x00090909, 0x000A0A0A, 0x000B0B0B, 0x000C0C0C, 0x000D0D0D, 0x000D0D0D, 0x000E0E0E,
		0x000F0F0F, 0x00101010, 0x00111111, 0x00111111, 0x00121212, 0x00131313, 0x00141414, 0x00141414,
		0x00151515, 0x00161616, 0x00161616, 0x00171717, 0x00181818, 0x00191919, 0x00191919, 0x001A1A1A,
		0x001B1B1B, 0x001B1B1B, 0x001C1C1C, 0x001D1D1D, 0x001E1E1E, 0x001E1E1E, 0x001F1F1F, 0x00202020,
		0x00212121, 0x00212121, 0x00222222, 0x00232323, 0x00242424, 0x00242424, 0x00252525, 0x00262626,
		0x00272727, 0x00282828, 0x00292929, 0x00292929, 0x002A2A2A, 0x002B2B2B, 0x002C2C2C, 0x002D2D2D,
		0x002E2E2E, 0x002F2F2F, 0x00303030, 0x00313131, 0x00313131, 0x00323232, 0x00333333, 0x00343434,
		0x00353535, 0x00363636, 0x00373737, 0x00383838, 0x00393939, 0x003A3A3A, 0x003B3B3B, 0x003C3C3C,
		0x003D3D3D, 0x003E3E3E, 0x003F3F3F, 0x00404040, 0x00414141, 0x00424242, 0x00434343, 0x00444444,
		0x00454545, 0x00464646, 0x00474747, 0x00484848, 0x004A4A4A, 0x004B4B4B, 0x004C4C4C, 0x004D4D4D,
		0x004E4E4E, 0x004F4F4F, 0x00505050, 0x00515151, 0x00525252, 0x00535353, 0x00555555, 0x00565656,
		0x00575757, 0x00585858, 0x00595959, 0x005A5A5A, 0x005B5B5B, 0x005C5C5C, 0x005E5E5E, 0x005F5F5F,
		0x00606060, 0x00616161, 0x00626262, 0x00636363, 0x00656565, 0x00666666, 0x00676767, 0x00686868,
		0x00696969, 0x006B6B6B, 0x006C6C6C, 0x006D6D6D, 0x006E6E6E, 0x006F6F6F, 0x00717171, 0x00727272,
		0x00737373, 0x00747474, 0x00757575, 0x00777777, 0x00787878, 0x00797979, 0x007A7A7A, 0x007B7B7B,
		0x007D7D7D, 0x007E7E7E, 0x007F7F7F, 0x00808080, 0x00828282, 0x00838383, 0x00848484, 0x00858585,
		0x00868686, 0x00888888, 0x00898989, 0x008A8A8A, 0x008B8B8B, 0x008D8D8D, 0x008E8E8E, 0x008F8F8F,
		0x00909090, 0x00929292, 0x00939393, 0x00949494, 0x00959595, 0x00979797, 0x00989898, 0x00999999,
		0x009A9A9A, 0x009B9B9B, 0x009D9D9D, 0x009E9E9E, 0x009F9F9F, 0x00A0A0A0, 0x00A2A2A2, 0x00A3A3A3,
		0x00A4A4A4, 0x00A5A5A5, 0x00A6A6A6, 0x00A8A8A8, 0x00A9A9A9, 0x00AAAAAA, 0x00ABABAB, 0x00ACACAC,
		0x00AEAEAE, 0x00AFAFAF, 0x00B0B0B0, 0x00B1B1B1, 0x00B2B2B2, 0x00B4B4B4, 0x00B5B5B5, 0x00B6B6B6,
		0x00B7B7B7, 0x00B8B8B8, 0x00B9B9B9, 0x00BBBBBB, 0x00BCBCBC, 0x00BDBDBD, 0x00BEBEBE, 0x00BFBFBF,
		0x00C0C0C0, 0x00C1C1C1, 0x00C3C3C3, 0x00C4C4C4, 0x00C5C5C5, 0x00C6C6C6, 0x00C7C7C7, 0x00C8C8C8,
		0x00C9C9C9, 0x00CACACA, 0x00CBCBCB, 0x00CDCDCD, 0x00CECECE, 0x00CFCFCF, 0x00D0D0D0, 0x00D1D1D1,
		0x00D2D2D2, 0x00D3D3D3, 0x00D4D4D4, 0x00D5D5D5, 0x00D6D6D6, 0x00D7D7D7, 0x00D8D8D8, 0x00D9D9D9,
		0x00DADADA, 0x00DBDBDB, 0x00DCDCDC, 0x00DDDDDD, 0x00DEDEDE, 0x00DFDFDF, 0x00E0E0E0, 0x00E1E1E1,
		0x00E2E2E2, 0x00E3E3E3, 0x00E4E4E4, 0x00E5E5E5, 0x00E5E5E5, 0x00E6E6E6, 0x00E7E7E7, 0x00E8E8E8,
		0x00E9E9E9, 0x00EAEAEA, 0x00EBEBEB, 0x00ECECEC, 0x00ECECEC, 0x00EDEDED, 0x00EEEEEE, 0x00EFEFEF,
		0x00F0F0F0, 0x00F0F0F0, 0x00F1F1F1, 0x00F2F2F2, 0x00F3F3F3, 0x00F3F3F3, 0x00F4F4F4, 0x00F5F5F5,
		0x00F6F6F6, 0x00F6F6F6, 0x00F7F7F7, 0x00F8F8F8, 0x00F8F8F8, 0x00F9F9F9, 0x00FAFAFA, 0x00FAFAFA,
		0x00FBFBFB, 0x00FCFCFC, 0x00FCFCFC, 0x00FDFDFD, 0x00FDFDFD, 0x00FEFEFE, 0x00FEFEFE, 0x00FFFFFF
	};

	/* !!! assume open HDMI before video start */
	if (gdisp.screen[sel].output_type == DISP_OUTPUT_TYPE_HDMI) {
		scaler_index = gdisp.screen[sel].layer_manage[id].scaler_index;
		scaleuprate =
			gdisp.screen[sel].layer_manage[id].para.scn_win.width *
			2 /
			gdisp.screen[sel].layer_manage[id].para.src_win.width;

		switch (scaleuprate) {
		case 0:	/* scale down, do noting */
			DE_SCAL_Vpp_Enable(scaler_index, 0);
			DE_SCAL_Vpp_Set_Luma_Sharpness_Level(scaler_index, 0);
			break;
		case 1:
			DE_SCAL_Vpp_Enable(scaler_index, 1);
			DE_SCAL_Vpp_Set_Luma_Sharpness_Level(scaler_index, 1);
			break;
		case 2:
			DE_SCAL_Vpp_Enable(scaler_index, 1);
			DE_SCAL_Vpp_Set_Luma_Sharpness_Level(scaler_index, 2);
			break;
		case 3:
			DE_SCAL_Vpp_Enable(scaler_index, 1);
			DE_SCAL_Vpp_Set_Luma_Sharpness_Level(scaler_index, 3);
			break;
		default:
			DE_SCAL_Vpp_Enable(scaler_index, 1);
			DE_SCAL_Vpp_Set_Luma_Sharpness_Level(scaler_index, 4);
			break;
		}

		TCON1_set_gamma_Enable(sel, 1);
		TCON1_set_gamma_table(sel, (__u32) gamma_tab, 1024);

		gdisp.screen[sel].layer_manage[id].video_enhancement_en = 1;
	}

	return 0;
}

static __s32 video_enhancement_stop(__u32 sel, __u32 id)
{
	__u32 scaler_index;

	if (gdisp.screen[sel].layer_manage[id].video_enhancement_en) {
		scaler_index = gdisp.screen[sel].layer_manage[id].scaler_index;

		DE_SCAL_Vpp_Enable(scaler_index, 0);
		DE_SCAL_Vpp_Set_Luma_Sharpness_Level(scaler_index, 0);

		if (gdisp.screen[sel].output_type != DISP_OUTPUT_TYPE_LCD)
			TCON1_set_gamma_Enable(sel, 0);

		gdisp.screen[sel].layer_manage[id].video_enhancement_en = 0;
	}

	return 0;
}
#endif /* CONFIG_ARCH_SUN4I */

static inline __s32 Hal_Set_Frame(__u32 sel, __u32 tcon_index, __u32 id)
{
	__u32 cur_line = 0, start_delay = 0;

	cur_line = LCDC_get_cur_line(sel, tcon_index);
	start_delay = LCDC_get_start_delay(sel, tcon_index);
	if (cur_line > start_delay - 5) {
#if 0
		DE_INF("cur_line(%d) >= start_delay(%d)-3 in Hal_Set_Frame\n",
		       cur_line, start_delay);
#endif
		return DIS_FAIL;
	}

	if (g_video[sel][id].display_cnt == 0) {
		g_video[sel][id].pre_frame_addr0 =
		    g_video[sel][id].video_cur.addr[0];
		memcpy(&g_video[sel][id].video_cur, &g_video[sel][id].video_new,
		       sizeof(__disp_video_fb_t));
	}

	if (gdisp.screen[sel].layer_manage[id].para.mode ==
	    DISP_LAYER_WORK_MODE_SCALER) {
		__u32 scaler_index;
		__scal_buf_addr_t scal_addr;
		__scal_src_size_t in_size;
		__scal_out_size_t out_size;
		__scal_src_type_t in_type;
		__scal_out_type_t out_type;
		__scal_scan_mod_t in_scan;
		__scal_scan_mod_t out_scan;
		__disp_scaler_t *scaler;
		__u32 pre_frame_addr = 0;
		__u32 maf_flag_addr = 0;
		__u32 maf_linestride = 0;

		scaler_index = gdisp.screen[sel].layer_manage[id].scaler_index;

		scaler = &(gdisp.scaler[scaler_index]);

		if (g_video[sel][id].video_cur.interlace == TRUE) {
#ifdef CONFIG_ARCH_SUN4I
			if ((!(gdisp.screen[sel].de_flicker_status &
			       DE_FLICKER_USED)) &&
			    (scaler->in_fb.format == DISP_FORMAT_YUV420 &&
			     scaler->in_fb.mode == DISP_MOD_MB_UV_COMBINED))
				g_video[sel][id].dit_enable = TRUE;
#else
			g_video[sel][id].dit_enable = FALSE;
#endif

#ifdef CONFIG_ARCH_SUN4I
			g_video[sel][id].fetch_field = FALSE;
#else
			g_video[sel][id].fetch_field = TRUE;
#endif
			if (g_video[sel][id].display_cnt == 0) {
				g_video[sel][id].fetch_bot =
					(g_video[sel][id].video_cur.
					 top_field_first) ? 0 : 1;
			} else {
				g_video[sel][id].fetch_bot =
					(g_video[sel][id].video_cur.
					 top_field_first) ? 1 : 0;
			}

			if (g_video[sel][id].dit_enable == TRUE) {
				if (g_video[sel][id].video_cur.maf_valid ==
				    TRUE) {
					g_video[sel][id].dit_mode =
						DIT_MODE_MAF;
					maf_flag_addr =
						g_video[sel][id].video_cur.
						flag_addr;
					maf_linestride =
						g_video[sel][id].video_cur.
						flag_stride;
				} else {
					g_video[sel][id].dit_mode =
						DIT_MODE_MAF_BOB;
				}

				if (g_video[sel][id].video_cur.
				    pre_frame_valid == TRUE) {
					g_video[sel][id].tempdiff_en = TRUE;
					pre_frame_addr = g_video[sel][id].
						pre_frame_addr0;
				} else {
					g_video[sel][id].tempdiff_en = FALSE;
				}
				g_video[sel][id].diagintp_en = TRUE;
#ifdef CONFIG_ARCH_SUN5I
				g_video[sel][id].fetch_field = FALSE;	//todo
				g_video[sel][id].fetch_bot = 0;	//todo
				// todo
				g_video[sel][id].dit_mode = DIT_MODE_MAF_BOB;
				g_video[sel][id].diagintp_en = FALSE;	//todo
#endif
				g_video[sel][id].tempdiff_en = FALSE;	//todo
			} else {
#ifdef CONFIG_ARCH_SUN5I
				g_video[sel][id].fetch_bot = FALSE;
#endif
				g_video[sel][id].dit_mode = DIT_MODE_WEAVE;
				g_video[sel][id].tempdiff_en = FALSE;
				g_video[sel][id].diagintp_en = FALSE;
			}
		} else {
			g_video[sel][id].dit_enable = FALSE;
			g_video[sel][id].fetch_field = FALSE;
			g_video[sel][id].fetch_bot = FALSE;
			g_video[sel][id].dit_mode = DIT_MODE_WEAVE;
			g_video[sel][id].tempdiff_en = FALSE;
			g_video[sel][id].diagintp_en = FALSE;
		}

		in_type.fmt = Scaler_sw_para_to_reg(0, scaler->in_fb.format);
		in_type.mod = Scaler_sw_para_to_reg(1, scaler->in_fb.mode);
		in_type.ps = Scaler_sw_para_to_reg(2, scaler->in_fb.seq);
		in_type.byte_seq = 0;
		in_type.sample_method = 0;

		scal_addr.ch0_addr = g_video[sel][id].video_cur.addr[0];
		scal_addr.ch1_addr = g_video[sel][id].video_cur.addr[1];
		scal_addr.ch2_addr = g_video[sel][id].video_cur.addr[2];

		in_size.src_width = scaler->in_fb.size.width;
		in_size.src_height = scaler->in_fb.size.height;
		in_size.x_off = scaler->src_win.x;
		in_size.y_off = scaler->src_win.y;
		in_size.scal_height = scaler->src_win.height;
		in_size.scal_width = scaler->src_win.width;

		out_type.byte_seq = scaler->out_fb.seq;
		out_type.fmt = scaler->out_fb.format;

		out_size.width = scaler->out_size.width;
		out_size.height = scaler->out_size.height;

		in_scan.field = g_video[sel][id].fetch_field;
		in_scan.bottom = g_video[sel][id].fetch_bot;

#ifdef CONFIG_ARCH_SUN4I
		out_scan.field = (gdisp.screen[sel].de_flicker_status &
				  DE_FLICKER_USED) ?
			0 : gdisp.screen[sel].b_out_interlace;
#else
		out_scan.field = (gdisp.screen[sel].iep_status ==
				  DE_FLICKER_USED) ?
			0 : gdisp.screen[sel].b_out_interlace;
#endif

		if (scaler->out_fb.cs_mode > DISP_VXYCC)
			scaler->out_fb.cs_mode = DISP_BT601;

		if (scaler->in_fb.b_trd_src) {
			__scal_3d_inmode_t inmode;
			__scal_3d_outmode_t outmode = 0;
			__scal_buf_addr_t scal_addr_right;

			inmode =
				Scaler_3d_sw_para_to_reg(0,
							 scaler->in_fb.trd_mode,
							 0);
			outmode =
				Scaler_3d_sw_para_to_reg(1,
							 scaler->out_trd_mode,
							 gdisp.screen[sel].
							 b_out_interlace);

			DE_SCAL_Get_3D_In_Single_Size(inmode, &in_size,
						      &in_size);
			if (scaler->b_trd_out) {
				DE_SCAL_Get_3D_Out_Single_Size(outmode,
							       &out_size,
							       &out_size);
			}

			scal_addr_right.ch0_addr =
				g_video[sel][id].video_cur.addr_right[0];
			scal_addr_right.ch1_addr =
				g_video[sel][id].video_cur.addr_right[1];
			scal_addr_right.ch2_addr =
				g_video[sel][id].video_cur.addr_right[2];

			DE_SCAL_Set_3D_Ctrl(scaler_index, scaler->b_trd_out,
					    inmode, outmode);
			DE_SCAL_Config_3D_Src(scaler_index, &scal_addr,
					      &in_size, &in_type, inmode,
					      &scal_addr_right);
		} else {
			DE_SCAL_Config_Src(scaler_index, &scal_addr, &in_size,
					   &in_type, FALSE, FALSE);
		}

		DE_SCAL_Set_Init_Phase(scaler_index, &in_scan, &in_size,
				       &in_type, &out_scan, &out_size,
				       &out_type, g_video[sel][id].dit_enable);
		DE_SCAL_Set_Scaling_Factor(scaler_index, &in_scan, &in_size,
					   &in_type, &out_scan, &out_size,
					   &out_type);
		DE_SCAL_Set_Scaling_Coef(scaler_index, &in_scan, &in_size,
					 &in_type, &out_scan, &out_size,
					 &out_type, scaler->smooth_mode);
		DE_SCAL_Set_Out_Size(scaler_index, &out_scan, &out_type,
				     &out_size);
		DE_SCAL_Set_Di_Ctrl(scaler_index, g_video[sel][id].dit_enable,
				    g_video[sel][id].dit_mode,
				    g_video[sel][id].diagintp_en,
				    g_video[sel][id].tempdiff_en);
		DE_SCAL_Set_Di_PreFrame_Addr(scaler_index, pre_frame_addr);
		DE_SCAL_Set_Di_MafFlag_Src(scaler_index, maf_flag_addr,
					   maf_linestride);

		DE_SCAL_Set_Reg_Rdy(scaler_index);
	} else {
		__layer_man_t *layer_man;
		__disp_fb_t fb;
		layer_src_t layer_fb;

		layer_man = &gdisp.screen[sel].layer_manage[id];

		BSP_disp_layer_get_framebuffer(sel, id, &fb);
		fb.addr[0] = g_video[sel][id].video_cur.addr[0];
		fb.addr[1] = g_video[sel][id].video_cur.addr[1];
		fb.addr[2] = g_video[sel][id].video_cur.addr[2];

		if (get_fb_type(fb.format) == DISP_FB_TYPE_YUV) {
			Yuv_Channel_adjusting(sel, fb.mode, fb.format,
					      &layer_man->para.src_win.x,
					      &layer_man->para.scn_win.width);
			Yuv_Channel_Set_framebuffer(sel, &fb,
						    layer_man->para.src_win.x,
						    layer_man->para.src_win.y);
		} else {
			layer_fb.fb_addr = fb.addr[0];
			layer_fb.pixseq = img_sw_para_to_reg(3, 0, fb.seq);
			layer_fb.br_swap = fb.br_swap;
			layer_fb.fb_width = fb.size.width;
			layer_fb.offset_x = layer_man->para.src_win.x;
			layer_fb.offset_y = layer_man->para.src_win.y;
			layer_fb.format = fb.format;
			DE_BE_Layer_Set_Framebuffer(sel, id, &layer_fb);
		}
	}

	g_video[sel][id].display_cnt++;
	gdisp.screen[sel].layer_manage[id].para.fb.addr[0] =
		g_video[sel][id].video_cur.addr[0];
	gdisp.screen[sel].layer_manage[id].para.fb.addr[1] =
		g_video[sel][id].video_cur.addr[1];
	gdisp.screen[sel].layer_manage[id].para.fb.addr[2] =
		g_video[sel][id].video_cur.addr[2];
	gdisp.screen[sel].layer_manage[id].para.fb.trd_right_addr[0] =
		g_video[sel][id].video_cur.addr_right[0];
	gdisp.screen[sel].layer_manage[id].para.fb.trd_right_addr[1] =
		g_video[sel][id].video_cur.addr_right[1];
	gdisp.screen[sel].layer_manage[id].para.fb.trd_right_addr[2] =
		g_video[sel][id].video_cur.addr_right[2];
	return DIS_SUCCESS;
}

__s32 Video_Operation_In_Vblanking(__u32 sel, __u32 tcon_index)
{
	__u32 id = 0;

	for (id = 0; id < 4; id++) {
		if ((g_video[sel][id].enable == TRUE) &&
		    (g_video[sel][id].have_got_frame == TRUE)) {
			Hal_Set_Frame(sel, tcon_index, id);
		}
	}

	return DIS_SUCCESS;
}

__s32 BSP_disp_video_set_fb(__u32 sel, __u32 hid, __disp_video_fb_t *in_addr)
{
	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (g_video[sel][hid].enable) {
		memcpy(&g_video[sel][hid].video_new, in_addr,
		       sizeof(__disp_video_fb_t));
		g_video[sel][hid].have_got_frame = TRUE;
		g_video[sel][hid].display_cnt = 0;

		return DIS_SUCCESS;
	} else
		return DIS_FAIL;
}

/*
 * get the current displaying frame id
 */
__s32 BSP_disp_video_get_frame_id(__u32 sel, __u32 hid)
{
	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (g_video[sel][hid].enable) {
		if (g_video[sel][hid].have_got_frame == TRUE)
			return g_video[sel][hid].video_cur.id;
		else
			return DIS_FAIL;
	} else
		return DIS_FAIL;
}

__s32 BSP_disp_video_get_dit_info(__u32 sel, __u32 hid,
				  __disp_dit_info_t *dit_info)
{
	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (g_video[sel][hid].enable) {
		dit_info->maf_enable = FALSE;
		dit_info->pre_frame_enable = FALSE;

		if (g_video[sel][hid].dit_enable) {
			if (g_video[sel][hid].dit_mode == DIT_MODE_MAF)
				dit_info->maf_enable = TRUE;

			if (g_video[sel][hid].tempdiff_en)
				dit_info->pre_frame_enable = TRUE;
		}
		return DIS_SUCCESS;
	} else
		return DIS_FAIL;
}

__s32 BSP_disp_video_start(__u32 sel, __u32 hid)
{
	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (gdisp.screen[sel].layer_manage[hid].status & LAYER_USED) {
		memset(&g_video[sel][hid], 0, sizeof(frame_para_t));
		g_video[sel][hid].video_cur.id = -1;
		g_video[sel][hid].enable = TRUE;

#ifdef CONFIG_ARCH_SUN4I
		video_enhancement_start(sel, hid);
#endif
		return DIS_SUCCESS;
	} else {
		return DIS_FAIL;
	}
}

__s32 BSP_disp_video_stop(__u32 sel, __u32 hid)
{
	hid = HANDTOID(hid);
	HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

	if (g_video[sel][hid].enable) {
		memset(&g_video[sel][hid], 0, sizeof(frame_para_t));

#ifdef CONFIG_ARCH_SUN4I
		video_enhancement_stop(sel, hid);
#endif
		return DIS_SUCCESS;
	} else {
		return DIS_FAIL;
	}
}
