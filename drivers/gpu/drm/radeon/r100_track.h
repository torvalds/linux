
#define R100_TRACK_MAX_TEXTURE 3
#define R200_TRACK_MAX_TEXTURE 6
#define R300_TRACK_MAX_TEXTURE 16

#define R100_MAX_CB 1
#define R300_MAX_CB 4

/*
 * CS functions
 */
struct r100_cs_track_cb {
	struct radeon_bo	*robj;
	unsigned		pitch;
	unsigned		cpp;
	unsigned		offset;
};

struct r100_cs_track_array {
	struct radeon_bo	*robj;
	unsigned		esize;
};

struct r100_cs_cube_info {
	struct radeon_bo	*robj;
	unsigned		offset;
	unsigned		width;
	unsigned		height;
};

#define R100_TRACK_COMP_NONE   0
#define R100_TRACK_COMP_DXT1   1
#define R100_TRACK_COMP_DXT35  2

struct r100_cs_track_texture {
	struct radeon_bo	*robj;
	struct r100_cs_cube_info cube_info[5]; /* info for 5 non-primary faces */
	unsigned		pitch;
	unsigned		width;
	unsigned		height;
	unsigned		num_levels;
	unsigned		cpp;
	unsigned		tex_coord_type;
	unsigned		txdepth;
	unsigned		width_11;
	unsigned		height_11;
	bool			use_pitch;
	bool			enabled;
	bool			roundup_w;
	bool			roundup_h;
	unsigned                compress_format;
};

struct r100_cs_track_limits {
	unsigned num_cb;
	unsigned num_texture;
	unsigned max_levels;
};

struct r100_cs_track {
	struct radeon_device *rdev;
	unsigned			num_cb;
	unsigned                        num_texture;
	unsigned			maxy;
	unsigned			vtx_size;
	unsigned			vap_vf_cntl;
	unsigned			immd_dwords;
	unsigned			num_arrays;
	unsigned			max_indx;
	unsigned			color_channel_mask;
	struct r100_cs_track_array	arrays[11];
	struct r100_cs_track_cb 	cb[R300_MAX_CB];
	struct r100_cs_track_cb 	zb;
	struct r100_cs_track_texture	textures[R300_TRACK_MAX_TEXTURE];
	bool				z_enabled;
	bool                            separate_cube;
	bool				fastfill;
	bool				blend_read_enable;
};

int r100_cs_track_check(struct radeon_device *rdev, struct r100_cs_track *track);
void r100_cs_track_clear(struct radeon_device *rdev, struct r100_cs_track *track);
int r100_cs_packet_next_reloc(struct radeon_cs_parser *p,
			      struct radeon_cs_reloc **cs_reloc);
void r100_cs_dump_packet(struct radeon_cs_parser *p,
			 struct radeon_cs_packet *pkt);

int r100_cs_packet_parse_vline(struct radeon_cs_parser *p);

int r200_packet0_check(struct radeon_cs_parser *p,
		       struct radeon_cs_packet *pkt,
		       unsigned idx, unsigned reg);



static inline int r100_reloc_pitch_offset(struct radeon_cs_parser *p,
					  struct radeon_cs_packet *pkt,
					  unsigned idx,
					  unsigned reg)
{
	int r;
	u32 tile_flags = 0;
	u32 tmp;
	struct radeon_cs_reloc *reloc;
	u32 value;

	r = r100_cs_packet_next_reloc(p, &reloc);
	if (r) {
		DRM_ERROR("No reloc for ib[%d]=0x%04X\n",
			  idx, reg);
		r100_cs_dump_packet(p, pkt);
		return r;
	}
	value = radeon_get_ib_value(p, idx);
	tmp = value & 0x003fffff;
	tmp += (((u32)reloc->lobj.gpu_offset) >> 10);

	if (reloc->lobj.tiling_flags & RADEON_TILING_MACRO)
		tile_flags |= RADEON_DST_TILE_MACRO;
	if (reloc->lobj.tiling_flags & RADEON_TILING_MICRO) {
		if (reg == RADEON_SRC_PITCH_OFFSET) {
			DRM_ERROR("Cannot src blit from microtiled surface\n");
			r100_cs_dump_packet(p, pkt);
			return -EINVAL;
		}
		tile_flags |= RADEON_DST_TILE_MICRO;
	}

	tmp |= tile_flags;
	p->ib->ptr[idx] = (value & 0x3fc00000) | tmp;
	return 0;
}

static inline int r100_packet3_load_vbpntr(struct radeon_cs_parser *p,
					   struct radeon_cs_packet *pkt,
					   int idx)
{
	unsigned c, i;
	struct radeon_cs_reloc *reloc;
	struct r100_cs_track *track;
	int r = 0;
	volatile uint32_t *ib;
	u32 idx_value;

	ib = p->ib->ptr;
	track = (struct r100_cs_track *)p->track;
	c = radeon_get_ib_value(p, idx++) & 0x1F;
	track->num_arrays = c;
	for (i = 0; i < (c - 1); i+=2, idx+=3) {
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n",
				  pkt->opcode);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		idx_value = radeon_get_ib_value(p, idx);
		ib[idx+1] = radeon_get_ib_value(p, idx + 1) + ((u32)reloc->lobj.gpu_offset);

		track->arrays[i + 0].esize = idx_value >> 8;
		track->arrays[i + 0].robj = reloc->robj;
		track->arrays[i + 0].esize &= 0x7F;
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n",
				  pkt->opcode);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		ib[idx+2] = radeon_get_ib_value(p, idx + 2) + ((u32)reloc->lobj.gpu_offset);
		track->arrays[i + 1].robj = reloc->robj;
		track->arrays[i + 1].esize = idx_value >> 24;
		track->arrays[i + 1].esize &= 0x7F;
	}
	if (c & 1) {
		r = r100_cs_packet_next_reloc(p, &reloc);
		if (r) {
			DRM_ERROR("No reloc for packet3 %d\n",
					  pkt->opcode);
			r100_cs_dump_packet(p, pkt);
			return r;
		}
		idx_value = radeon_get_ib_value(p, idx);
		ib[idx+1] = radeon_get_ib_value(p, idx + 1) + ((u32)reloc->lobj.gpu_offset);
		track->arrays[i + 0].robj = reloc->robj;
		track->arrays[i + 0].esize = idx_value >> 8;
		track->arrays[i + 0].esize &= 0x7F;
	}
	return r;
}
