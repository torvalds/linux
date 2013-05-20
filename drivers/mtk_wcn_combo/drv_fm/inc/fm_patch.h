#ifndef __FM_PATCH_H__
#define __FM_PATCH_H__

enum {
    FM_ROM_V1 = 0,
    FM_ROM_V2 = 1,
    FM_ROM_V3 = 2,
    FM_ROM_V4 = 3,
    FM_ROM_V5 = 4,
    FM_ROM_MAX
};

struct fm_patch_tbl {
    fm_s32 idx;
    fm_s8 *patch;
    fm_s8 *coeff;
    fm_s8 *rom;
    fm_s8 *hwcoeff;
};

extern fm_s32 fm_file_exist(const fm_s8 *filename);

extern fm_s32 fm_file_read(const fm_s8 *filename, fm_u8* dst, fm_s32 len, fm_s32 position);

extern fm_s32 fm_file_write(const fm_s8 *filename, fm_u8* dst, fm_s32 len, fm_s32 *ppos);


#endif //__FM_PATCH_H__

