/**************************************************************************
 * Copyright © 2014-2015 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
#ifndef VMW_SO_H
#define VMW_SO_H

enum vmw_view_type {
	vmw_view_sr,
	vmw_view_rt,
	vmw_view_ds,
	vmw_view_max,
};

enum vmw_so_type {
	vmw_so_el,
	vmw_so_bs,
	vmw_so_ds,
	vmw_so_rs,
	vmw_so_ss,
	vmw_so_so,
	vmw_so_max,
};

/**
 * union vmw_view_destroy - view destruction command body
 *
 * @rtv: RenderTarget view destruction command body
 * @srv: ShaderResource view destruction command body
 * @dsv: DepthStencil view destruction command body
 * @view_id: A single u32 view id.
 *
 * The assumption here is that all union members are really represented by a
 * single u32 in the command stream. If that's not the case,
 * the size of this union will not equal the size of an u32, and the
 * assumption is invalid, and we detect that at compile time in the
 * vmw_so_build_asserts() function.
 */
union vmw_view_destroy {
	struct SVGA3dCmdDXDestroyRenderTargetView rtv;
	struct SVGA3dCmdDXDestroyShaderResourceView srv;
	struct SVGA3dCmdDXDestroyDepthStencilView dsv;
	u32 view_id;
};

/* Map enum vmw_view_type to view destroy command ids*/
extern const u32 vmw_view_destroy_cmds[];

/* Map enum vmw_view_type to SVGACOTableType */
extern const SVGACOTableType vmw_view_cotables[];

/* Map enum vmw_so_type to SVGACOTableType */
extern const SVGACOTableType vmw_so_cotables[];

/*
 * vmw_view_cmd_to_type - Return the view type for a create or destroy command
 *
 * @id: The SVGA3D command id.
 *
 * For a given view create or destroy command id, return the corresponding
 * enum vmw_view_type. If the command is unknown, return vmw_view_max.
 * The validity of the simplified calculation is verified in the
 * vmw_so_build_asserts() function.
 */
static inline enum vmw_view_type vmw_view_cmd_to_type(u32 id)
{
	u32 tmp = (id - SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW) / 2;

	if (tmp > (u32)vmw_view_max)
		return vmw_view_max;

	return (enum vmw_view_type) tmp;
}

/*
 * vmw_so_cmd_to_type - Return the state object type for a
 * create or destroy command
 *
 * @id: The SVGA3D command id.
 *
 * For a given state object create or destroy command id,
 * return the corresponding enum vmw_so_type. If the command is uknown,
 * return vmw_so_max. We should perhaps optimize this function using
 * a similar strategy as vmw_view_cmd_to_type().
 */
static inline enum vmw_so_type vmw_so_cmd_to_type(u32 id)
{
	switch (id) {
	case SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT:
	case SVGA_3D_CMD_DX_DESTROY_ELEMENTLAYOUT:
		return vmw_so_el;
	case SVGA_3D_CMD_DX_DEFINE_BLEND_STATE:
	case SVGA_3D_CMD_DX_DESTROY_BLEND_STATE:
		return vmw_so_bs;
	case SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE:
	case SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_STATE:
		return vmw_so_ds;
	case SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE:
	case SVGA_3D_CMD_DX_DESTROY_RASTERIZER_STATE:
		return vmw_so_rs;
	case SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE:
	case SVGA_3D_CMD_DX_DESTROY_SAMPLER_STATE:
		return vmw_so_ss;
	case SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT:
	case SVGA_3D_CMD_DX_DESTROY_STREAMOUTPUT:
		return vmw_so_so;
	default:
		break;
	}
	return vmw_so_max;
}

/*
 * View management - vmwgfx_so.c
 */
extern int vmw_view_add(struct vmw_cmdbuf_res_manager *man,
			struct vmw_resource *ctx,
			struct vmw_resource *srf,
			enum vmw_view_type view_type,
			u32 user_key,
			const void *cmd,
			size_t cmd_size,
			struct list_head *list);

extern int vmw_view_remove(struct vmw_cmdbuf_res_manager *man,
			   u32 user_key, enum vmw_view_type view_type,
			   struct list_head *list,
			   struct vmw_resource **res_p);

extern void vmw_view_surface_list_destroy(struct vmw_private *dev_priv,
					  struct list_head *view_list);
extern void vmw_view_cotable_list_destroy(struct vmw_private *dev_priv,
					  struct list_head *list,
					  bool readback);
extern struct vmw_resource *vmw_view_srf(struct vmw_resource *res);
extern struct vmw_resource *vmw_view_lookup(struct vmw_cmdbuf_res_manager *man,
					    enum vmw_view_type view_type,
					    u32 user_key);
#endif
