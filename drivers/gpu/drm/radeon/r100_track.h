
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
	bool                    lookup_disable;
	bool			roundup_w;
	bool			roundup_h;
	unsigned                compress_format;
};

struct r100_cs_track {
	unsigned			num_cb;
	unsigned                        num_texture;
	unsigned			maxy;
	unsigned			vtx_size;
	unsigned			vap_vf_cntl;
	unsigned			vap_alt_nverts;
	unsigned			immd_dwords;
	unsigned			num_arrays;
	unsigned			max_indx;
	unsigned			color_channel_mask;
	struct r100_cs_track_array	arrays[16];
	struct r100_cs_track_cb 	cb[R300_MAX_CB];
	struct r100_cs_track_cb 	zb;
	struct r100_cs_track_cb 	aa;
	struct r100_cs_track_texture	textures[R300_TRACK_MAX_TEXTURE];
	bool				z_enabled;
	bool                            separate_cube;
	bool				zb_cb_clear;
	bool				blend_read_enable;
	bool				cb_dirty;
	bool				zb_dirty;
	bool				tex_dirty;
	bool				aa_dirty;
	bool				aaresolve;
};

int r100_cs_track_check(struct radeon_device *rdev, struct r100_cs_track *track);
void r100_cs_track_clear(struct radeon_device *rdev, struct r100_cs_track *track);

int r100_cs_packet_parse_vline(struct radeon_cs_parser *p);

int r200_packet0_check(struct radeon_cs_parser *p,
		       struct radeon_cs_packet *pkt,
		       unsigned idx, unsigned reg);

int r100_reloc_pitch_offset(struct radeon_cs_parser *p,
			    struct radeon_cs_packet *pkt,
			    unsigned idx,
			    unsigned reg);
int r100_packet3_load_vbpntr(struct radeon_cs_parser *p,
			     struct radeon_cs_packet *pkt,
			     int idx);
