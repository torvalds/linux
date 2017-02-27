/*
 * Copyright © 2014 Broadcom
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * DOC: Command list validator for VC4.
 *
 * The VC4 has no IOMMU between it and system memory.  So, a user with
 * access to execute command lists could escalate privilege by
 * overwriting system memory (drawing to it as a framebuffer) or
 * reading system memory it shouldn't (reading it as a texture, or
 * uniform data, or vertex data).
 *
 * This validates command lists to ensure that all accesses are within
 * the bounds of the GEM objects referenced.  It explicitly whitelists
 * packets, and looks at the offsets in any address fields to make
 * sure they're constrained within the BOs they reference.
 *
 * Note that because of the validation that's happening anyway, this
 * is where GEM relocation processing happens.
 */

#include "uapi/drm/vc4_drm.h"
#include "vc4_drv.h"
#include "vc4_packet.h"

#define VALIDATE_ARGS \
	struct vc4_exec_info *exec,			\
	void *validated,				\
	void *untrusted

/** Return the width in pixels of a 64-byte microtile. */
static uint32_t
utile_width(int cpp)
{
	switch (cpp) {
	case 1:
	case 2:
		return 8;
	case 4:
		return 4;
	case 8:
		return 2;
	default:
		DRM_ERROR("unknown cpp: %d\n", cpp);
		return 1;
	}
}

/** Return the height in pixels of a 64-byte microtile. */
static uint32_t
utile_height(int cpp)
{
	switch (cpp) {
	case 1:
		return 8;
	case 2:
	case 4:
	case 8:
		return 4;
	default:
		DRM_ERROR("unknown cpp: %d\n", cpp);
		return 1;
	}
}

/**
 * size_is_lt() - Returns whether a miplevel of the given size will
 * use the lineartile (LT) tiling layout rather than the normal T
 * tiling layout.
 * @width: Width in pixels of the miplevel
 * @height: Height in pixels of the miplevel
 * @cpp: Bytes per pixel of the pixel format
 */
static bool
size_is_lt(uint32_t width, uint32_t height, int cpp)
{
	return (width <= 4 * utile_width(cpp) ||
		height <= 4 * utile_height(cpp));
}

struct drm_gem_cma_object *
vc4_use_bo(struct vc4_exec_info *exec, uint32_t hindex)
{
	struct drm_gem_cma_object *obj;
	struct vc4_bo *bo;

	if (hindex >= exec->bo_count) {
		DRM_ERROR("BO index %d greater than BO count %d\n",
			  hindex, exec->bo_count);
		return NULL;
	}
	obj = exec->bo[hindex];
	bo = to_vc4_bo(&obj->base);

	if (bo->validated_shader) {
		DRM_ERROR("Trying to use shader BO as something other than "
			  "a shader\n");
		return NULL;
	}

	return obj;
}

static struct drm_gem_cma_object *
vc4_use_handle(struct vc4_exec_info *exec, uint32_t gem_handles_packet_index)
{
	return vc4_use_bo(exec, exec->bo_index[gem_handles_packet_index]);
}

static bool
validate_bin_pos(struct vc4_exec_info *exec, void *untrusted, uint32_t pos)
{
	/* Note that the untrusted pointer passed to these functions is
	 * incremented past the packet byte.
	 */
	return (untrusted - 1 == exec->bin_u + pos);
}

static uint32_t
gl_shader_rec_size(uint32_t pointer_bits)
{
	uint32_t attribute_count = pointer_bits & 7;
	bool extended = pointer_bits & 8;

	if (attribute_count == 0)
		attribute_count = 8;

	if (extended)
		return 100 + attribute_count * 4;
	else
		return 36 + attribute_count * 8;
}

bool
vc4_check_tex_size(struct vc4_exec_info *exec, struct drm_gem_cma_object *fbo,
		   uint32_t offset, uint8_t tiling_format,
		   uint32_t width, uint32_t height, uint8_t cpp)
{
	uint32_t aligned_width, aligned_height, stride, size;
	uint32_t utile_w = utile_width(cpp);
	uint32_t utile_h = utile_height(cpp);

	/* The shaded vertex format stores signed 12.4 fixed point
	 * (-2048,2047) offsets from the viewport center, so we should
	 * never have a render target larger than 4096.  The texture
	 * unit can only sample from 2048x2048, so it's even more
	 * restricted.  This lets us avoid worrying about overflow in
	 * our math.
	 */
	if (width > 4096 || height > 4096) {
		DRM_ERROR("Surface dimesions (%d,%d) too large", width, height);
		return false;
	}

	switch (tiling_format) {
	case VC4_TILING_FORMAT_LINEAR:
		aligned_width = round_up(width, utile_w);
		aligned_height = height;
		break;
	case VC4_TILING_FORMAT_T:
		aligned_width = round_up(width, utile_w * 8);
		aligned_height = round_up(height, utile_h * 8);
		break;
	case VC4_TILING_FORMAT_LT:
		aligned_width = round_up(width, utile_w);
		aligned_height = round_up(height, utile_h);
		break;
	default:
		DRM_ERROR("buffer tiling %d unsupported\n", tiling_format);
		return false;
	}

	stride = aligned_width * cpp;
	size = stride * aligned_height;

	if (size + offset < size ||
	    size + offset > fbo->base.size) {
		DRM_ERROR("Overflow in %dx%d (%dx%d) fbo size (%d + %d > %zd)\n",
			  width, height,
			  aligned_width, aligned_height,
			  size, offset, fbo->base.size);
		return false;
	}

	return true;
}

static int
validate_flush(VALIDATE_ARGS)
{
	if (!validate_bin_pos(exec, untrusted, exec->args->bin_cl_size - 1)) {
		DRM_ERROR("Bin CL must end with VC4_PACKET_FLUSH\n");
		return -EINVAL;
	}
	exec->found_flush = true;

	return 0;
}

static int
validate_start_tile_binning(VALIDATE_ARGS)
{
	if (exec->found_start_tile_binning_packet) {
		DRM_ERROR("Duplicate VC4_PACKET_START_TILE_BINNING\n");
		return -EINVAL;
	}
	exec->found_start_tile_binning_packet = true;

	if (!exec->found_tile_binning_mode_config_packet) {
		DRM_ERROR("missing VC4_PACKET_TILE_BINNING_MODE_CONFIG\n");
		return -EINVAL;
	}

	return 0;
}

static int
validate_increment_semaphore(VALIDATE_ARGS)
{
	if (!validate_bin_pos(exec, untrusted, exec->args->bin_cl_size - 2)) {
		DRM_ERROR("Bin CL must end with "
			  "VC4_PACKET_INCREMENT_SEMAPHORE\n");
		return -EINVAL;
	}
	exec->found_increment_semaphore_packet = true;

	return 0;
}

static int
validate_indexed_prim_list(VALIDATE_ARGS)
{
	struct drm_gem_cma_object *ib;
	uint32_t length = *(uint32_t *)(untrusted + 1);
	uint32_t offset = *(uint32_t *)(untrusted + 5);
	uint32_t max_index = *(uint32_t *)(untrusted + 9);
	uint32_t index_size = (*(uint8_t *)(untrusted + 0) >> 4) ? 2 : 1;
	struct vc4_shader_state *shader_state;

	/* Check overflow condition */
	if (exec->shader_state_count == 0) {
		DRM_ERROR("shader state must precede primitives\n");
		return -EINVAL;
	}
	shader_state = &exec->shader_state[exec->shader_state_count - 1];

	if (max_index > shader_state->max_index)
		shader_state->max_index = max_index;

	ib = vc4_use_handle(exec, 0);
	if (!ib)
		return -EINVAL;

	exec->bin_dep_seqno = max(exec->bin_dep_seqno,
				  to_vc4_bo(&ib->base)->write_seqno);

	if (offset > ib->base.size ||
	    (ib->base.size - offset) / index_size < length) {
		DRM_ERROR("IB access overflow (%d + %d*%d > %zd)\n",
			  offset, length, index_size, ib->base.size);
		return -EINVAL;
	}

	*(uint32_t *)(validated + 5) = ib->paddr + offset;

	return 0;
}

static int
validate_gl_array_primitive(VALIDATE_ARGS)
{
	uint32_t length = *(uint32_t *)(untrusted + 1);
	uint32_t base_index = *(uint32_t *)(untrusted + 5);
	uint32_t max_index;
	struct vc4_shader_state *shader_state;

	/* Check overflow condition */
	if (exec->shader_state_count == 0) {
		DRM_ERROR("shader state must precede primitives\n");
		return -EINVAL;
	}
	shader_state = &exec->shader_state[exec->shader_state_count - 1];

	if (length + base_index < length) {
		DRM_ERROR("primitive vertex count overflow\n");
		return -EINVAL;
	}
	max_index = length + base_index - 1;

	if (max_index > shader_state->max_index)
		shader_state->max_index = max_index;

	return 0;
}

static int
validate_gl_shader_state(VALIDATE_ARGS)
{
	uint32_t i = exec->shader_state_count++;

	if (i >= exec->shader_state_size) {
		DRM_ERROR("More requests for shader states than declared\n");
		return -EINVAL;
	}

	exec->shader_state[i].addr = *(uint32_t *)untrusted;
	exec->shader_state[i].max_index = 0;

	if (exec->shader_state[i].addr & ~0xf) {
		DRM_ERROR("high bits set in GL shader rec reference\n");
		return -EINVAL;
	}

	*(uint32_t *)validated = (exec->shader_rec_p +
				  exec->shader_state[i].addr);

	exec->shader_rec_p +=
		roundup(gl_shader_rec_size(exec->shader_state[i].addr), 16);

	return 0;
}

static int
validate_tile_binning_config(VALIDATE_ARGS)
{
	struct drm_device *dev = exec->exec_bo->base.dev;
	struct vc4_bo *tile_bo;
	uint8_t flags;
	uint32_t tile_state_size, tile_alloc_size;
	uint32_t tile_count;

	if (exec->found_tile_binning_mode_config_packet) {
		DRM_ERROR("Duplicate VC4_PACKET_TILE_BINNING_MODE_CONFIG\n");
		return -EINVAL;
	}
	exec->found_tile_binning_mode_config_packet = true;

	exec->bin_tiles_x = *(uint8_t *)(untrusted + 12);
	exec->bin_tiles_y = *(uint8_t *)(untrusted + 13);
	tile_count = exec->bin_tiles_x * exec->bin_tiles_y;
	flags = *(uint8_t *)(untrusted + 14);

	if (exec->bin_tiles_x == 0 ||
	    exec->bin_tiles_y == 0) {
		DRM_ERROR("Tile binning config of %dx%d too small\n",
			  exec->bin_tiles_x, exec->bin_tiles_y);
		return -EINVAL;
	}

	if (flags & (VC4_BIN_CONFIG_DB_NON_MS |
		     VC4_BIN_CONFIG_TILE_BUFFER_64BIT)) {
		DRM_ERROR("unsupported binning config flags 0x%02x\n", flags);
		return -EINVAL;
	}

	/* The tile state data array is 48 bytes per tile, and we put it at
	 * the start of a BO containing both it and the tile alloc.
	 */
	tile_state_size = 48 * tile_count;

	/* Since the tile alloc array will follow us, align. */
	exec->tile_alloc_offset = roundup(tile_state_size, 4096);

	*(uint8_t *)(validated + 14) =
		((flags & ~(VC4_BIN_CONFIG_ALLOC_INIT_BLOCK_SIZE_MASK |
			    VC4_BIN_CONFIG_ALLOC_BLOCK_SIZE_MASK)) |
		 VC4_BIN_CONFIG_AUTO_INIT_TSDA |
		 VC4_SET_FIELD(VC4_BIN_CONFIG_ALLOC_INIT_BLOCK_SIZE_32,
			       VC4_BIN_CONFIG_ALLOC_INIT_BLOCK_SIZE) |
		 VC4_SET_FIELD(VC4_BIN_CONFIG_ALLOC_BLOCK_SIZE_128,
			       VC4_BIN_CONFIG_ALLOC_BLOCK_SIZE));

	/* Initial block size. */
	tile_alloc_size = 32 * tile_count;

	/*
	 * The initial allocation gets rounded to the next 256 bytes before
	 * the hardware starts fulfilling further allocations.
	 */
	tile_alloc_size = roundup(tile_alloc_size, 256);

	/* Add space for the extra allocations.  This is what gets used first,
	 * before overflow memory.  It must have at least 4096 bytes, but we
	 * want to avoid overflow memory usage if possible.
	 */
	tile_alloc_size += 1024 * 1024;

	tile_bo = vc4_bo_create(dev, exec->tile_alloc_offset + tile_alloc_size,
				true);
	exec->tile_bo = &tile_bo->base;
	if (IS_ERR(exec->tile_bo))
		return PTR_ERR(exec->tile_bo);
	list_add_tail(&tile_bo->unref_head, &exec->unref_list);

	/* tile alloc address. */
	*(uint32_t *)(validated + 0) = (exec->tile_bo->paddr +
					exec->tile_alloc_offset);
	/* tile alloc size. */
	*(uint32_t *)(validated + 4) = tile_alloc_size;
	/* tile state address. */
	*(uint32_t *)(validated + 8) = exec->tile_bo->paddr;

	return 0;
}

static int
validate_gem_handles(VALIDATE_ARGS)
{
	memcpy(exec->bo_index, untrusted, sizeof(exec->bo_index));
	return 0;
}

#define VC4_DEFINE_PACKET(packet, func) \
	[packet] = { packet ## _SIZE, #packet, func }

static const struct cmd_info {
	uint16_t len;
	const char *name;
	int (*func)(struct vc4_exec_info *exec, void *validated,
		    void *untrusted);
} cmd_info[] = {
	VC4_DEFINE_PACKET(VC4_PACKET_HALT, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_NOP, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_FLUSH, validate_flush),
	VC4_DEFINE_PACKET(VC4_PACKET_FLUSH_ALL, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_START_TILE_BINNING,
			  validate_start_tile_binning),
	VC4_DEFINE_PACKET(VC4_PACKET_INCREMENT_SEMAPHORE,
			  validate_increment_semaphore),

	VC4_DEFINE_PACKET(VC4_PACKET_GL_INDEXED_PRIMITIVE,
			  validate_indexed_prim_list),
	VC4_DEFINE_PACKET(VC4_PACKET_GL_ARRAY_PRIMITIVE,
			  validate_gl_array_primitive),

	VC4_DEFINE_PACKET(VC4_PACKET_PRIMITIVE_LIST_FORMAT, NULL),

	VC4_DEFINE_PACKET(VC4_PACKET_GL_SHADER_STATE, validate_gl_shader_state),

	VC4_DEFINE_PACKET(VC4_PACKET_CONFIGURATION_BITS, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_FLAT_SHADE_FLAGS, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_POINT_SIZE, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_LINE_WIDTH, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_RHT_X_BOUNDARY, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_DEPTH_OFFSET, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_CLIP_WINDOW, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_VIEWPORT_OFFSET, NULL),
	VC4_DEFINE_PACKET(VC4_PACKET_CLIPPER_XY_SCALING, NULL),
	/* Note: The docs say this was also 105, but it was 106 in the
	 * initial userland code drop.
	 */
	VC4_DEFINE_PACKET(VC4_PACKET_CLIPPER_Z_SCALING, NULL),

	VC4_DEFINE_PACKET(VC4_PACKET_TILE_BINNING_MODE_CONFIG,
			  validate_tile_binning_config),

	VC4_DEFINE_PACKET(VC4_PACKET_GEM_HANDLES, validate_gem_handles),
};

int
vc4_validate_bin_cl(struct drm_device *dev,
		    void *validated,
		    void *unvalidated,
		    struct vc4_exec_info *exec)
{
	uint32_t len = exec->args->bin_cl_size;
	uint32_t dst_offset = 0;
	uint32_t src_offset = 0;

	while (src_offset < len) {
		void *dst_pkt = validated + dst_offset;
		void *src_pkt = unvalidated + src_offset;
		u8 cmd = *(uint8_t *)src_pkt;
		const struct cmd_info *info;

		if (cmd >= ARRAY_SIZE(cmd_info)) {
			DRM_ERROR("0x%08x: packet %d out of bounds\n",
				  src_offset, cmd);
			return -EINVAL;
		}

		info = &cmd_info[cmd];
		if (!info->name) {
			DRM_ERROR("0x%08x: packet %d invalid\n",
				  src_offset, cmd);
			return -EINVAL;
		}

		if (src_offset + info->len > len) {
			DRM_ERROR("0x%08x: packet %d (%s) length 0x%08x "
				  "exceeds bounds (0x%08x)\n",
				  src_offset, cmd, info->name, info->len,
				  src_offset + len);
			return -EINVAL;
		}

		if (cmd != VC4_PACKET_GEM_HANDLES)
			memcpy(dst_pkt, src_pkt, info->len);

		if (info->func && info->func(exec,
					     dst_pkt + 1,
					     src_pkt + 1)) {
			DRM_ERROR("0x%08x: packet %d (%s) failed to validate\n",
				  src_offset, cmd, info->name);
			return -EINVAL;
		}

		src_offset += info->len;
		/* GEM handle loading doesn't produce HW packets. */
		if (cmd != VC4_PACKET_GEM_HANDLES)
			dst_offset += info->len;

		/* When the CL hits halt, it'll stop reading anything else. */
		if (cmd == VC4_PACKET_HALT)
			break;
	}

	exec->ct0ea = exec->ct0ca + dst_offset;

	if (!exec->found_start_tile_binning_packet) {
		DRM_ERROR("Bin CL missing VC4_PACKET_START_TILE_BINNING\n");
		return -EINVAL;
	}

	/* The bin CL must be ended with INCREMENT_SEMAPHORE and FLUSH.  The
	 * semaphore is used to trigger the render CL to start up, and the
	 * FLUSH is what caps the bin lists with
	 * VC4_PACKET_RETURN_FROM_SUB_LIST (so they jump back to the main
	 * render CL when they get called to) and actually triggers the queued
	 * semaphore increment.
	 */
	if (!exec->found_increment_semaphore_packet || !exec->found_flush) {
		DRM_ERROR("Bin CL missing VC4_PACKET_INCREMENT_SEMAPHORE + "
			  "VC4_PACKET_FLUSH\n");
		return -EINVAL;
	}

	return 0;
}

static bool
reloc_tex(struct vc4_exec_info *exec,
	  void *uniform_data_u,
	  struct vc4_texture_sample_info *sample,
	  uint32_t texture_handle_index, bool is_cs)
{
	struct drm_gem_cma_object *tex;
	uint32_t p0 = *(uint32_t *)(uniform_data_u + sample->p_offset[0]);
	uint32_t p1 = *(uint32_t *)(uniform_data_u + sample->p_offset[1]);
	uint32_t p2 = (sample->p_offset[2] != ~0 ?
		       *(uint32_t *)(uniform_data_u + sample->p_offset[2]) : 0);
	uint32_t p3 = (sample->p_offset[3] != ~0 ?
		       *(uint32_t *)(uniform_data_u + sample->p_offset[3]) : 0);
	uint32_t *validated_p0 = exec->uniforms_v + sample->p_offset[0];
	uint32_t offset = p0 & VC4_TEX_P0_OFFSET_MASK;
	uint32_t miplevels = VC4_GET_FIELD(p0, VC4_TEX_P0_MIPLVLS);
	uint32_t width = VC4_GET_FIELD(p1, VC4_TEX_P1_WIDTH);
	uint32_t height = VC4_GET_FIELD(p1, VC4_TEX_P1_HEIGHT);
	uint32_t cpp, tiling_format, utile_w, utile_h;
	uint32_t i;
	uint32_t cube_map_stride = 0;
	enum vc4_texture_data_type type;

	tex = vc4_use_bo(exec, texture_handle_index);
	if (!tex)
		return false;

	if (sample->is_direct) {
		uint32_t remaining_size = tex->base.size - p0;

		if (p0 > tex->base.size - 4) {
			DRM_ERROR("UBO offset greater than UBO size\n");
			goto fail;
		}
		if (p1 > remaining_size - 4) {
			DRM_ERROR("UBO clamp would allow reads "
				  "outside of UBO\n");
			goto fail;
		}
		*validated_p0 = tex->paddr + p0;
		return true;
	}

	if (width == 0)
		width = 2048;
	if (height == 0)
		height = 2048;

	if (p0 & VC4_TEX_P0_CMMODE_MASK) {
		if (VC4_GET_FIELD(p2, VC4_TEX_P2_PTYPE) ==
		    VC4_TEX_P2_PTYPE_CUBE_MAP_STRIDE)
			cube_map_stride = p2 & VC4_TEX_P2_CMST_MASK;
		if (VC4_GET_FIELD(p3, VC4_TEX_P2_PTYPE) ==
		    VC4_TEX_P2_PTYPE_CUBE_MAP_STRIDE) {
			if (cube_map_stride) {
				DRM_ERROR("Cube map stride set twice\n");
				goto fail;
			}

			cube_map_stride = p3 & VC4_TEX_P2_CMST_MASK;
		}
		if (!cube_map_stride) {
			DRM_ERROR("Cube map stride not set\n");
			goto fail;
		}
	}

	type = (VC4_GET_FIELD(p0, VC4_TEX_P0_TYPE) |
		(VC4_GET_FIELD(p1, VC4_TEX_P1_TYPE4) << 4));

	switch (type) {
	case VC4_TEXTURE_TYPE_RGBA8888:
	case VC4_TEXTURE_TYPE_RGBX8888:
	case VC4_TEXTURE_TYPE_RGBA32R:
		cpp = 4;
		break;
	case VC4_TEXTURE_TYPE_RGBA4444:
	case VC4_TEXTURE_TYPE_RGBA5551:
	case VC4_TEXTURE_TYPE_RGB565:
	case VC4_TEXTURE_TYPE_LUMALPHA:
	case VC4_TEXTURE_TYPE_S16F:
	case VC4_TEXTURE_TYPE_S16:
		cpp = 2;
		break;
	case VC4_TEXTURE_TYPE_LUMINANCE:
	case VC4_TEXTURE_TYPE_ALPHA:
	case VC4_TEXTURE_TYPE_S8:
		cpp = 1;
		break;
	case VC4_TEXTURE_TYPE_ETC1:
		/* ETC1 is arranged as 64-bit blocks, where each block is 4x4
		 * pixels.
		 */
		cpp = 8;
		width = (width + 3) >> 2;
		height = (height + 3) >> 2;
		break;
	case VC4_TEXTURE_TYPE_BW1:
	case VC4_TEXTURE_TYPE_A4:
	case VC4_TEXTURE_TYPE_A1:
	case VC4_TEXTURE_TYPE_RGBA64:
	case VC4_TEXTURE_TYPE_YUV422R:
	default:
		DRM_ERROR("Texture format %d unsupported\n", type);
		goto fail;
	}
	utile_w = utile_width(cpp);
	utile_h = utile_height(cpp);

	if (type == VC4_TEXTURE_TYPE_RGBA32R) {
		tiling_format = VC4_TILING_FORMAT_LINEAR;
	} else {
		if (size_is_lt(width, height, cpp))
			tiling_format = VC4_TILING_FORMAT_LT;
		else
			tiling_format = VC4_TILING_FORMAT_T;
	}

	if (!vc4_check_tex_size(exec, tex, offset + cube_map_stride * 5,
				tiling_format, width, height, cpp)) {
		goto fail;
	}

	/* The mipmap levels are stored before the base of the texture.  Make
	 * sure there is actually space in the BO.
	 */
	for (i = 1; i <= miplevels; i++) {
		uint32_t level_width = max(width >> i, 1u);
		uint32_t level_height = max(height >> i, 1u);
		uint32_t aligned_width, aligned_height;
		uint32_t level_size;

		/* Once the levels get small enough, they drop from T to LT. */
		if (tiling_format == VC4_TILING_FORMAT_T &&
		    size_is_lt(level_width, level_height, cpp)) {
			tiling_format = VC4_TILING_FORMAT_LT;
		}

		switch (tiling_format) {
		case VC4_TILING_FORMAT_T:
			aligned_width = round_up(level_width, utile_w * 8);
			aligned_height = round_up(level_height, utile_h * 8);
			break;
		case VC4_TILING_FORMAT_LT:
			aligned_width = round_up(level_width, utile_w);
			aligned_height = round_up(level_height, utile_h);
			break;
		default:
			aligned_width = round_up(level_width, utile_w);
			aligned_height = level_height;
			break;
		}

		level_size = aligned_width * cpp * aligned_height;

		if (offset < level_size) {
			DRM_ERROR("Level %d (%dx%d -> %dx%d) size %db "
				  "overflowed buffer bounds (offset %d)\n",
				  i, level_width, level_height,
				  aligned_width, aligned_height,
				  level_size, offset);
			goto fail;
		}

		offset -= level_size;
	}

	*validated_p0 = tex->paddr + p0;

	if (is_cs) {
		exec->bin_dep_seqno = max(exec->bin_dep_seqno,
					  to_vc4_bo(&tex->base)->write_seqno);
	}

	return true;
 fail:
	DRM_INFO("Texture p0 at %d: 0x%08x\n", sample->p_offset[0], p0);
	DRM_INFO("Texture p1 at %d: 0x%08x\n", sample->p_offset[1], p1);
	DRM_INFO("Texture p2 at %d: 0x%08x\n", sample->p_offset[2], p2);
	DRM_INFO("Texture p3 at %d: 0x%08x\n", sample->p_offset[3], p3);
	return false;
}

static int
validate_gl_shader_rec(struct drm_device *dev,
		       struct vc4_exec_info *exec,
		       struct vc4_shader_state *state)
{
	uint32_t *src_handles;
	void *pkt_u, *pkt_v;
	static const uint32_t shader_reloc_offsets[] = {
		4, /* fs */
		16, /* vs */
		28, /* cs */
	};
	uint32_t shader_reloc_count = ARRAY_SIZE(shader_reloc_offsets);
	struct drm_gem_cma_object *bo[shader_reloc_count + 8];
	uint32_t nr_attributes, nr_relocs, packet_size;
	int i;

	nr_attributes = state->addr & 0x7;
	if (nr_attributes == 0)
		nr_attributes = 8;
	packet_size = gl_shader_rec_size(state->addr);

	nr_relocs = ARRAY_SIZE(shader_reloc_offsets) + nr_attributes;
	if (nr_relocs * 4 > exec->shader_rec_size) {
		DRM_ERROR("overflowed shader recs reading %d handles "
			  "from %d bytes left\n",
			  nr_relocs, exec->shader_rec_size);
		return -EINVAL;
	}
	src_handles = exec->shader_rec_u;
	exec->shader_rec_u += nr_relocs * 4;
	exec->shader_rec_size -= nr_relocs * 4;

	if (packet_size > exec->shader_rec_size) {
		DRM_ERROR("overflowed shader recs copying %db packet "
			  "from %d bytes left\n",
			  packet_size, exec->shader_rec_size);
		return -EINVAL;
	}
	pkt_u = exec->shader_rec_u;
	pkt_v = exec->shader_rec_v;
	memcpy(pkt_v, pkt_u, packet_size);
	exec->shader_rec_u += packet_size;
	/* Shader recs have to be aligned to 16 bytes (due to the attribute
	 * flags being in the low bytes), so round the next validated shader
	 * rec address up.  This should be safe, since we've got so many
	 * relocations in a shader rec packet.
	 */
	BUG_ON(roundup(packet_size, 16) - packet_size > nr_relocs * 4);
	exec->shader_rec_v += roundup(packet_size, 16);
	exec->shader_rec_size -= packet_size;

	for (i = 0; i < shader_reloc_count; i++) {
		if (src_handles[i] > exec->bo_count) {
			DRM_ERROR("Shader handle %d too big\n", src_handles[i]);
			return -EINVAL;
		}

		bo[i] = exec->bo[src_handles[i]];
		if (!bo[i])
			return -EINVAL;
	}
	for (i = shader_reloc_count; i < nr_relocs; i++) {
		bo[i] = vc4_use_bo(exec, src_handles[i]);
		if (!bo[i])
			return -EINVAL;
	}

	if (((*(uint16_t *)pkt_u & VC4_SHADER_FLAG_FS_SINGLE_THREAD) == 0) !=
	    to_vc4_bo(&bo[0]->base)->validated_shader->is_threaded) {
		DRM_ERROR("Thread mode of CL and FS do not match\n");
		return -EINVAL;
	}

	if (to_vc4_bo(&bo[1]->base)->validated_shader->is_threaded ||
	    to_vc4_bo(&bo[2]->base)->validated_shader->is_threaded) {
		DRM_ERROR("cs and vs cannot be threaded\n");
		return -EINVAL;
	}

	for (i = 0; i < shader_reloc_count; i++) {
		struct vc4_validated_shader_info *validated_shader;
		uint32_t o = shader_reloc_offsets[i];
		uint32_t src_offset = *(uint32_t *)(pkt_u + o);
		uint32_t *texture_handles_u;
		void *uniform_data_u;
		uint32_t tex, uni;

		*(uint32_t *)(pkt_v + o) = bo[i]->paddr + src_offset;

		if (src_offset != 0) {
			DRM_ERROR("Shaders must be at offset 0 of "
				  "the BO.\n");
			return -EINVAL;
		}

		validated_shader = to_vc4_bo(&bo[i]->base)->validated_shader;
		if (!validated_shader)
			return -EINVAL;

		if (validated_shader->uniforms_src_size >
		    exec->uniforms_size) {
			DRM_ERROR("Uniforms src buffer overflow\n");
			return -EINVAL;
		}

		texture_handles_u = exec->uniforms_u;
		uniform_data_u = (texture_handles_u +
				  validated_shader->num_texture_samples);

		memcpy(exec->uniforms_v, uniform_data_u,
		       validated_shader->uniforms_size);

		for (tex = 0;
		     tex < validated_shader->num_texture_samples;
		     tex++) {
			if (!reloc_tex(exec,
				       uniform_data_u,
				       &validated_shader->texture_samples[tex],
				       texture_handles_u[tex],
				       i == 2)) {
				return -EINVAL;
			}
		}

		/* Fill in the uniform slots that need this shader's
		 * start-of-uniforms address (used for resetting the uniform
		 * stream in the presence of control flow).
		 */
		for (uni = 0;
		     uni < validated_shader->num_uniform_addr_offsets;
		     uni++) {
			uint32_t o = validated_shader->uniform_addr_offsets[uni];
			((uint32_t *)exec->uniforms_v)[o] = exec->uniforms_p;
		}

		*(uint32_t *)(pkt_v + o + 4) = exec->uniforms_p;

		exec->uniforms_u += validated_shader->uniforms_src_size;
		exec->uniforms_v += validated_shader->uniforms_size;
		exec->uniforms_p += validated_shader->uniforms_size;
	}

	for (i = 0; i < nr_attributes; i++) {
		struct drm_gem_cma_object *vbo =
			bo[ARRAY_SIZE(shader_reloc_offsets) + i];
		uint32_t o = 36 + i * 8;
		uint32_t offset = *(uint32_t *)(pkt_u + o + 0);
		uint32_t attr_size = *(uint8_t *)(pkt_u + o + 4) + 1;
		uint32_t stride = *(uint8_t *)(pkt_u + o + 5);
		uint32_t max_index;

		exec->bin_dep_seqno = max(exec->bin_dep_seqno,
					  to_vc4_bo(&vbo->base)->write_seqno);

		if (state->addr & 0x8)
			stride |= (*(uint32_t *)(pkt_u + 100 + i * 4)) & ~0xff;

		if (vbo->base.size < offset ||
		    vbo->base.size - offset < attr_size) {
			DRM_ERROR("BO offset overflow (%d + %d > %zu)\n",
				  offset, attr_size, vbo->base.size);
			return -EINVAL;
		}

		if (stride != 0) {
			max_index = ((vbo->base.size - offset - attr_size) /
				     stride);
			if (state->max_index > max_index) {
				DRM_ERROR("primitives use index %d out of "
					  "supplied %d\n",
					  state->max_index, max_index);
				return -EINVAL;
			}
		}

		*(uint32_t *)(pkt_v + o) = vbo->paddr + offset;
	}

	return 0;
}

int
vc4_validate_shader_recs(struct drm_device *dev,
			 struct vc4_exec_info *exec)
{
	uint32_t i;
	int ret = 0;

	for (i = 0; i < exec->shader_state_count; i++) {
		ret = validate_gl_shader_rec(dev, exec, &exec->shader_state[i]);
		if (ret)
			return ret;
	}

	return ret;
}
