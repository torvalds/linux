#include <linux/string.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>

#include <mach/am_regs.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>

#include "deinterlace.h"

#ifdef DEBUG
unsigned di_pre_underflow = 0, di_pre_overflow = 0;
unsigned long debug_array[4 * 1024];
#endif

#if 1

#define RECEIVER_NAME "amvideo"

#else

#define RECEIVER_NAME "deinterlace"

#endif

#define PATTERN32_NUM       2
#define PATTERN22_NUM       32
#if (PATTERN22_NUM < 32)
#define PATTERN22_MARK      ((1LL<<PATTERN22_NUM)-1)
#elif (PATTERN22_NUM < 64)
#define PATTERN22_MARK      ((0x100000000LL<<(PATTERN22_NUM-32))-1)
#else
#define PATTERN22_MARK      0xffffffffffffffffLL
#endif

#define PRE_HOLD_LINE       4

#define DI_PRE_INTERVAL     (HZ/100)

// 0 - off
// 1 - pre-post link
// 2 - pre-post separate, only post in vsync
static int deinterlace_mode = 0;

#if defined(CONFIG_ARCH_MESON2)
static int noise_reduction_level = 2;
#endif

static struct timer_list di_pre_timer;
static struct work_struct di_pre_work;

int di_pre_recycle_buf = -1;

int prev_struct = 0;
int prog_field_count = 0;
int buf_recycle_done = 1;
int di_pre_post_done = 1;

int field_counter = 0, pre_field_counter = 0, di_checked_field = 0;

int pattern_len = 0;

int di_p32_counter = 0;
unsigned int last_big_data = 0, last_big_num = 0;

unsigned long blend_mode, pattern_22, di_info[4][83];
unsigned long long di_p32_info, di_p22_info, di_p32_info_2, di_p22_info_2;

vframe_t *cur_buf;
vframe_t di_buf_pool[DI_BUF_NUM];

DI_MIF_t di_inp_top_mif;
DI_MIF_t di_inp_bot_mif;
DI_MIF_t di_mem_mif;
DI_MIF_t di_buf0_mif;
DI_MIF_t di_buf1_mif;
DI_MIF_t di_chan2_mif;
DI_SIM_MIF_t di_nrwr_mif;
DI_SIM_MIF_t di_mtnwr_mif;
DI_SIM_MIF_t di_mtncrd_mif;
DI_SIM_MIF_t di_mtnprd_mif;

unsigned di_mem_start;

int vdin_en = 0;
vframe_t dummy_buf;

int get_deinterlace_mode(void)
{
    return deinterlace_mode;
}

void set_deinterlace_mode(int mode)
{
    deinterlace_mode = mode;
}

#if defined(CONFIG_ARCH_MESON2)
int get_noise_reduction_level(void)
{
    return noise_reduction_level;
}

void set_noise_reduction_level(int level)
{
    noise_reduction_level = level;
}
#endif

int get_di_pre_recycle_buf(void)
{
    return di_pre_recycle_buf;
}

vframe_t *peek_di_out_buf(void)
{
    if (field_counter <= pre_field_counter - 2) {
        return &(di_buf_pool[field_counter % DI_BUF_NUM]);
    } else {
        return NULL;
    }
}

void inc_field_counter(void)
{
    field_counter++;
}

void set_post_di_mem(int mode)
{
    unsigned temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((field_counter + di_checked_field) % DI_BUF_NUM);
    canvas_config(di_buf0_mif.canvas0_addr0, temp, MAX_CANVAS_WIDTH * 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
    if (mode == 1) {
        temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((field_counter + di_checked_field + 1) % DI_BUF_NUM);
    } else {
        temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((field_counter + di_checked_field - 1) % DI_BUF_NUM);
    }
    canvas_config(di_buf1_mif.canvas0_addr0, temp, MAX_CANVAS_WIDTH * 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
    temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((field_counter + di_checked_field) % DI_BUF_NUM) + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT);
    canvas_config(di_mtncrd_mif.canvas_num, temp, MAX_CANVAS_WIDTH / 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
    temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((field_counter + di_checked_field + 1) % DI_BUF_NUM) + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT);
    canvas_config(di_mtnprd_mif.canvas_num, temp, MAX_CANVAS_WIDTH / 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
}

void disable_deinterlace(void)
{
    WRITE_MPEG_REG(DI_PRE_CTRL, 0x3 << 30);
    WRITE_MPEG_REG(DI_POST_CTRL, 0x3 << 30);
    WRITE_MPEG_REG(DI_PRE_SIZE, (32 - 1) | ((64 - 1) << 16));
    WRITE_MPEG_REG(DI_POST_SIZE, (32 - 1) | ((128 - 1) << 16));
    WRITE_MPEG_REG(DI_INP_GEN_REG, READ_MPEG_REG(DI_INP_GEN_REG) & 0xfffffffe);
    WRITE_MPEG_REG(DI_MEM_GEN_REG, READ_MPEG_REG(DI_MEM_GEN_REG) & 0xfffffffe);
    WRITE_MPEG_REG(DI_CHAN2_GEN_REG, READ_MPEG_REG(DI_CHAN2_GEN_REG) & 0xfffffffe);
    WRITE_MPEG_REG(DI_IF1_GEN_REG, READ_MPEG_REG(DI_IF1_GEN_REG) & 0xfffffffe);
}

void disable_pre_deinterlace(void)
{
    unsigned status = READ_MPEG_REG(DI_PRE_CTRL) & 0x2;

    if (prev_struct > 0) {
        unsigned temp = READ_MPEG_REG(DI_PRE_SIZE);
        unsigned total = (temp & 0xffff) * ((temp >> 16) & 0xffff);
        unsigned count = 0;

        while ((READ_MPEG_REG(DI_INTR_CTRL) & 0xf) != (status | 0x9)) {
            if (count++ >= total) {
                break;
            }
        }

        WRITE_MPEG_REG(DI_INTR_CTRL, READ_MPEG_REG(DI_INTR_CTRL));
    }

    WRITE_MPEG_REG(DI_INP_GEN_REG, READ_MPEG_REG(DI_INP_GEN_REG) & 0xfffffffe);
    WRITE_MPEG_REG(DI_MEM_GEN_REG, READ_MPEG_REG(DI_MEM_GEN_REG) & 0xfffffffe);
    WRITE_MPEG_REG(DI_CHAN2_GEN_REG, READ_MPEG_REG(DI_CHAN2_GEN_REG) & 0xfffffffe);

#ifdef DEBUG
    di_pre_underflow = 0;
    di_pre_overflow = 0;
#endif

    prev_struct = 0;
    prog_field_count = 0;
    buf_recycle_done = 1;
    di_pre_post_done = 1;

    pre_field_counter = field_counter;

    di_pre_recycle_buf = -1;

    WRITE_MPEG_REG(DI_PRE_CTRL, 0x3 << 30);
    WRITE_MPEG_REG(DI_PRE_SIZE, (32 - 1) | ((64 - 1) << 16));
}

void disable_post_deinterlace(void)
{
    WRITE_MPEG_REG(DI_POST_CTRL, 0x3 << 30);
    WRITE_MPEG_REG(DI_POST_SIZE, (32 - 1) | ((128 - 1) << 16));
    WRITE_MPEG_REG(DI_IF1_GEN_REG, READ_MPEG_REG(DI_IF1_GEN_REG) & 0xfffffffe);
}

void set_vd1_fmt_more(
    int hfmt_en,
    int hz_yc_ratio,        //2bit
    int hz_ini_phase,       //4bit
    int vfmt_en,
    int vt_yc_ratio,        //2bit
    int vt_ini_phase,       //4bit
    int y_length,
    int c_length,
    int hz_rpt              //1bit
)
{
    int vt_phase_step = (16 >> vt_yc_ratio);

    WRITE_MPEG_REG(VIU_VD1_FMT_CTRL, (hz_rpt << 28) |       // hz rpt pixel
                   (hz_ini_phase << 24) |        // hz ini phase
                   (0 << 23) |                   // repeat p0 enable
                   (hz_yc_ratio << 21) |         // hz yc ratio
                   (hfmt_en << 20) |             // hz enable
                   (1 << 17) |                   // nrpt_phase0 enable
                   (0 << 16) |                   // repeat l0 enable
                   (0 << 12) |                   // skip line num
                   (vt_ini_phase << 8) |         // vt ini phase
                   (vt_phase_step << 1) |        // vt phase step (3.4)
                   (vfmt_en << 0)                // vt enable
                  );

    WRITE_MPEG_REG(VIU_VD1_FMT_W, (y_length << 16) |        // hz format width
                   (c_length << 0)                // vt format width
                  );
}

void initial_di_prepost(int hsize_pre, int vsize_pre, int hsize_post, int vsize_post, int hold_line)
{
    WRITE_MPEG_REG(DI_PRE_SIZE, (hsize_pre - 1) | ((vsize_pre - 1) << 16));
    WRITE_MPEG_REG(DI_POST_SIZE, (hsize_post - 1) | ((vsize_post - 1) << 16));
    WRITE_MPEG_REG(DI_BLEND_CTRL,
                   (0x2 << 20) |                     // top mode. EI only
                   25);                             // KDEINT
    WRITE_MPEG_REG(DI_EI_CTRL0, (255 << 16) |           // ei_filter.
                   (5 << 8) |                        // ei_threshold.
                   (1 << 2) |                        // ei bypass cf2.
                   (1 << 1));                        // ei bypass far1
    WRITE_MPEG_REG(DI_EI_CTRL1, (180 << 24) |           // ei diff
                   (10 << 16) |                      // ei ang45
                   (15 << 8) |                       // ei peak.
                   45);                             // ei cross.
    WRITE_MPEG_REG(DI_EI_CTRL2, (10 << 23) |            // close2
                   (10 << 16) |                      // close1
                   (10 << 8) |                       // far2
                   10);                             // far1
    WRITE_MPEG_REG(DI_PRE_CTRL, 0 |                     // NR enable
                   (0 << 1) |                          // MTN_EN
                   (0 << 2) |                          // check 3:2 pulldown
                   (0 << 3) |                          // check 2:2 pulldown
                   (0 << 4) |                          // 2:2 check mid pixel come from next field after MTN.
                   (0 << 5) |                          // hist check enable
                   (0 << 6) |                          // hist check not use chan2.
                   (0 << 7) |                          // hist check use data before noise reduction.
                   (0 << 8) |                          // chan 2 enable for 2:2 pull down check.
                   (0 << 9) |                          // line buffer 2 enable
                   (0 << 10) |                         // pre drop first.
                   (0 << 11) |                         // pre repeat.
                   (1 << 12) |                         // pre viu link
                   (hold_line << 16) |                 // pre hold line number
                   (0 << 29) |                         // pre field number.
                   (0x3 << 30)                         // pre soft rst, pre frame rst.
                  );

    WRITE_MPEG_REG(DI_POST_CTRL, (0 << 0) |         // line buffer 0 enable
                   (0 << 1)  |                       // line buffer 1 enable
                   (0 << 2) |                        // ei  enable
                   (0 << 3) |                        // mtn line buffer enable
                   (0 << 4) |                        // mtnp read mif enable
                   (0 << 5) |                        // di blend enble.
                   (0 << 6) |                        // di mux output enable
                   (0 << 7) |                        // di write to SDRAM enable.
                   (1 << 8) |                        // di to VPP enable.
                   (0 << 9) |                        // mif0 to VPP enable.
                   (0 << 10) |                       // post drop first.
                   (0 << 11) |                       // post repeat.
                   (1 << 12) |                       // post viu link
                   (1 << 13) |                       // prepost_link
                   (hold_line << 16) |               // post hold line number
                   (0 << 29) |                       // post field number.
                   (0x3 << 30)                       // post soft rst  post frame rst.
                  );

    WRITE_MPEG_REG(DI_MC_22LVL0, (READ_MPEG_REG(DI_MC_22LVL0) & 0xffff0000) | 256);             // field 22 level
    WRITE_MPEG_REG(DI_MC_32LVL0, (READ_MPEG_REG(DI_MC_32LVL0) & 0xffffff00) | 16);              // field 32 level

    // set hold line for all ddr req interface.
    WRITE_MPEG_REG(DI_INP_GEN_REG, (hold_line << 19));
    WRITE_MPEG_REG(DI_MEM_GEN_REG, (hold_line << 19));
    WRITE_MPEG_REG(VD1_IF0_GEN_REG, (hold_line << 19));
    WRITE_MPEG_REG(DI_IF1_GEN_REG, (hold_line << 19));
    WRITE_MPEG_REG(DI_CHAN2_GEN_REG, (hold_line << 19));
}

void initial_di_pre(int hsize_pre, int vsize_pre, int hold_line)
{
    WRITE_MPEG_REG(DI_PRE_SIZE, (hsize_pre - 1) | ((vsize_pre - 1) << 16));
    WRITE_MPEG_REG(DI_PRE_CTRL, 0 |             // NR enable
                   (0 << 1) |                  // MTN_EN
                   (0 << 2) |                  // check 3:2 pulldown
                   (0 << 3) |                  // check 2:2 pulldown
                   (0 << 4) |                  // 2:2 check mid pixel come from next field after MTN.
                   (0 << 5) |                  // hist check enable
                   (0 << 6) |                  // hist check not use chan2.
                   (0 << 7) |                  // hist check use data before noise reduction.
                   (0 << 8) |                  // chan 2 enable for 2:2 pull down check.
                   (0 << 9) |                  // line buffer 2 enable
                   (0 << 10) |                 // pre drop first.
                   (0 << 11) |                 // pre repeat.
                   (0 << 12) |                 // pre viu link
                   (hold_line << 16) |         // pre hold line number
                   (0 << 29) |                 // pre field number.
                   (0x3 << 30)                 // pre soft rst, pre frame rst.
                  );

    WRITE_MPEG_REG(DI_MC_22LVL0, (READ_MPEG_REG(DI_MC_22LVL0) & 0xffff0000) | 256);                 //   field 22 level
    WRITE_MPEG_REG(DI_MC_32LVL0, (READ_MPEG_REG(DI_MC_32LVL0) & 0xffffff00) | 16);                      // field 32 level
}

void initial_di_post(int hsize_post, int vsize_post, int hold_line)
{
    WRITE_MPEG_REG(DI_POST_SIZE, (hsize_post - 1) | ((vsize_post - 1) << 16));
    WRITE_MPEG_REG(DI_BLEND_CTRL,
                   (0x2 << 20) |                     // top mode. EI only
                   25);                             // KDEINT
    WRITE_MPEG_REG(DI_EI_CTRL0, (255 << 16) |           // ei_filter.
                   (5 << 8) |                        // ei_threshold.
                   (1 << 2) |                        // ei bypass cf2.
                   (1 << 1));                        // ei bypass far1
    WRITE_MPEG_REG(DI_EI_CTRL1, (180 << 24) |           // ei diff
                   (10 << 16) |                      // ei ang45
                   (15 << 8) |                       // ei peak.
                   45);                             // ei cross.
    WRITE_MPEG_REG(DI_EI_CTRL2, (10 << 23) |            // close2
                   (10 << 16) |                      // close1
                   (10 << 8) |                       // far2
                   10);                             // far1
    WRITE_MPEG_REG(DI_POST_CTRL, (0 << 0) |                 // line buffer 0 enable
                   (0 << 1)  |                       // line buffer 1 enable
                   (0 << 2)  |                       // ei  enable
                   (0 << 3)  |                       // mtn line buffer enable
                   (0 << 4)  |                       // mtnp read mif enable
                   (0 << 5)  |                       // di blend enble.
                   (0 << 6)  |                       // di mux output enable
                   (0 << 7)  |                       // di write to SDRAM enable.
                   (1 << 8)  |                       // di to VPP enable.
                   (0 << 9)  |                       // mif0 to VPP enable.
                   (0 << 10) |                       // post drop first.
                   (0 << 11) |                       // post repeat.
                   (1 << 12) |                       // post viu link
                   (hold_line << 16) |               // post hold line number
                   (0 << 29) |                       // post field number.
                   (0x3 << 30)                       // post soft rst  post frame rst.
                  );
}

void enable_di_mode_check(int win0_start_x, int win0_end_x, int win0_start_y, int win0_end_y,
                          int win1_start_x, int win1_end_x, int win1_start_y, int win1_end_y,
                          int win2_start_x, int win2_end_x, int win2_start_y, int win2_end_y,
                          int win3_start_x, int win3_end_x, int win3_start_y, int win3_end_y,
                          int win4_start_x, int win4_end_x, int win4_start_y, int win4_end_y,
                          int win0_32lvl,   int win1_32lvl, int win2_32lvl, int win3_32lvl, int win4_32lvl,
                          int win0_22lvl,   int win1_22lvl, int win2_22lvl, int win3_22lvl, int win4_22lvl,
                          int field_32lvl,  int field_22lvl)
{
    WRITE_MPEG_REG(DI_MC_REG0_X, (win0_start_x << 16) |         // start_x
                   win0_end_x);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG0_Y, (win0_start_y << 16) |         // start_y
                   win0_end_y);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG1_X, (win1_start_x << 16) |         // start_x
                   win1_end_x);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG1_Y, (win1_start_y << 16) |         // start_y
                   win1_end_y);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG2_X, (win2_start_x << 16) |         // start_x
                   win2_end_x);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG2_Y, (win2_start_y << 16) |         // start_y
                   win2_end_y);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG3_X, (win3_start_x << 16) |         // start_x
                   win3_end_x);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG3_Y, (win3_start_y << 16) |         // start_y
                   win3_end_y);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG4_X, (win4_start_x << 16) |         // start_x
                   win4_end_x);                             // end_x
    WRITE_MPEG_REG(DI_MC_REG4_Y, (win4_start_y << 16) |         // start_y
                   win4_end_y);                             // end_x

    WRITE_MPEG_REG(DI_MC_32LVL1, win3_32lvl |                   //region 3
                   (win4_32lvl << 8));                        //region 4
    WRITE_MPEG_REG(DI_MC_32LVL0, field_32lvl        |           //field 32 level
                   (win0_32lvl << 8)  |                       //region 0
                   (win1_32lvl << 16) |                       //region 1
                   (win2_32lvl << 24));                       //region 2.
    WRITE_MPEG_REG(DI_MC_22LVL0,  field_22lvl  |                // field 22 level
                   (win0_22lvl << 16));                       // region 0.

    WRITE_MPEG_REG(DI_MC_22LVL1,  win1_22lvl  |                 // region 1
                   (win2_22lvl << 16));                       // region 2.

    WRITE_MPEG_REG(DI_MC_22LVL2, win3_22lvl  |                  // region 3
                   (win4_22lvl << 16));                       // region 4.
    WRITE_MPEG_REG(DI_MC_CTRL, 0x1f);                           // enable region level
}

// handle all case of prepost link.
void enable_di_prepost_full(
    DI_MIF_t        *di_inp_mif,
    DI_MIF_t        *di_mem_mif,
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_MIF_t        *di_chan2_mif,
    DI_SIM_MIF_t    *di_nrwr_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtnwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int nr_en, int mtn_en, int pd32_check_en, int pd22_check_en, int hist_check_en,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
    int nr_hfilt_en, int nr_hfilt_mb_en, int mtn_modify_en,
    int blend_mtn_filt_en, int blend_data_filt_en, int post_mb_en,
#endif
    int post_field_num, int pre_field_num, int prepost_link, int hold_line)
{
    int hist_check_only;
    int ei_only;
    int buf1_en;

#if defined(CONFIG_ARCH_MESON2)
    int nr_zone_0, nr_zone_1, nr_zone_2;

    if (noise_reduction_level == 0) {
        nr_zone_0 = 1;
        nr_zone_1 = 3;
        nr_zone_2 = 5;
    } else {
        nr_zone_0 = 3;
        nr_zone_1 = 6;
        nr_zone_2 = 10;
    }
#endif

    hist_check_only = hist_check_en && !nr_en && !mtn_en && !pd22_check_en && !pd32_check_en;
    ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en);
#if defined(CONFIG_ARCH_MESON)
    buf1_en = (!prepost_link && !ei_only && (di_ddr_en || di_vpp_en));
#elif defined(CONFIG_ARCH_MESON2)
    if (ei_only) {
        buf1_en = 0;
    } else {
        buf1_en = 1;
    }
#endif

    if (nr_en | mtn_en | pd22_check_en || pd32_check_en) {
        set_di_inp_mif(di_inp_mif, di_vpp_en && prepost_link , hold_line);
        set_di_mem_mif(di_mem_mif, di_vpp_en && prepost_link, hold_line);
    }

    if (pd22_check_en || hist_check_only) {
        set_di_chan2_mif(di_chan2_mif, di_vpp_en && prepost_link, hold_line);
    }

#if defined(CONFIG_ARCH_MESON)
    if (ei_en || di_vpp_en || di_ddr_en) {
        set_di_if0_mif(di_buf0_mif, di_vpp_en, hold_line);
    }

    if (!prepost_link && !ei_only && (di_ddr_en || di_vpp_en)) {
        set_di_if1_mif(di_buf1_mif, di_vpp_en, hold_line);
    }
#elif defined(CONFIG_ARCH_MESON2)
    if (prepost_link && !ei_only && (di_ddr_en || di_vpp_en)) {
        set_di_if1_mif(di_buf1_mif, di_vpp_en, hold_line);
    } else if (!prepost_link && (ei_en || di_vpp_en || di_ddr_en)) {
        set_di_if0_mif(di_buf0_mif, di_vpp_en, hold_line);
        set_di_if1_mif(di_buf1_mif, di_vpp_en, hold_line);
    }
#endif

    // set nr wr mif interface.
    if (nr_en) {
        WRITE_MPEG_REG(DI_NRWR_X, (di_nrwr_mif->start_x << 16) | (di_nrwr_mif->end_x));     // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_NRWR_Y, (di_nrwr_mif->start_y << 16) | (di_nrwr_mif->end_y));     // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_NRWR_CTRL, di_nrwr_mif->canvas_num |                              // canvas index.
                       ((prepost_link && di_vpp_en) << 8));                                 // urgent.
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
        WRITE_MPEG_REG(DI_NR_CTRL0, (1 << 31) |                                             // nr yuv enable.
                       (1 << 30) |                                                         // nr range. 3 point
                       (0 << 29) |                                                         // max of 3 point.
                       (nr_hfilt_en << 28) |                                               // nr hfilter enable.
                       (nr_hfilt_mb_en << 27) |                                            // nr hfilter motion_blur enable.
                       (nr_zone_2 << 16) |                                                 // zone 2
                       (nr_zone_1 << 8) |                                                  // zone 1
                       (nr_zone_0 << 0));                                                  // zone 0
        WRITE_MPEG_REG(DI_NR_CTRL2, (10 << 24) |                                             //intra noise level
                       (1 << 16)  |                                                         // intra no noise level.
                       (10 << 8) |                                                          // inter noise level.
                       (1 << 0));                                                           // inter no noise level.
        WRITE_MPEG_REG(DI_NR_CTRL3, (16 << 16) |                                             // if any one of 3 point  mtn larger than 16 don't use 3 point.
                       720);                                                               // if one line eq cnt is larger than this number, this line is not conunted.
#endif
    }

    // motion wr mif.
    if (mtn_en) {
        WRITE_MPEG_REG(DI_MTNWR_X, (di_mtnwr_mif->start_x << 16) | (di_mtnwr_mif->end_x));      // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNWR_Y, (di_mtnwr_mif->start_y << 16) | (di_mtnwr_mif->end_y));      // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_MTNWR_CTRL, di_mtnwr_mif->canvas_num |                                // canvas index.
                       ((prepost_link && di_vpp_en) << 8));                                       // urgent.
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
        WRITE_MPEG_REG(DI_MTN_CTRL, (1 << 31) |                                                 // lpf enable.
                       (1 << 30) |                                                            // mtn uv enable.
                       (mtn_modify_en << 29) |                                                // no mtn modify.
                       (2 << 24) |                                                            // char diff count.
                       (40 << 16) |                                                           // black level.
                       (196 << 8) |                                                           // white level.
                       (64 << 0));                                                            // char diff level.
        WRITE_MPEG_REG(DI_MTN_CTRL1, (3 << 8) |                                                 // mtn shift if mtn modifty_en
                       0);                                                                   // mtn reduce before shift.
#endif
    }

    // motion for current display field.
#if defined(CONFIG_ARCH_MESON)
    if (blend_mtn_en) {
        WRITE_MPEG_REG(DI_MTNCRD_X, (di_mtncrd_mif->start_x << 16) | (di_mtncrd_mif->end_x));           // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNCRD_Y, (di_mtncrd_mif->start_y << 16) | (di_mtncrd_mif->end_y));           // start_y 0 end_y 239.
        if (!prepost_link) {
            WRITE_MPEG_REG(DI_MTNRD_CTRL, (di_mtnprd_mif->canvas_num << 8) |                            //mtnp canvas index.
                           (0 << 16) |                                                                 // urgent
                           di_mtncrd_mif->canvas_num);                                                 // current field mtn canvas index.
        } else {
            WRITE_MPEG_REG(DI_MTNRD_CTRL, (0 << 8) |                                                    //mtnp canvas index.
                           ((prepost_link && di_vpp_en) << 16)  |                                      // urgent
                           di_mtncrd_mif->canvas_num);                                                 // current field mtn canvas index.
        }
    }

    if (blend_mtn_en && !prepost_link) {
        WRITE_MPEG_REG(DI_MTNPRD_X, (di_mtnprd_mif->start_x << 16) | (di_mtnprd_mif->end_x));           // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNPRD_Y, (di_mtnprd_mif->start_y << 16) | (di_mtnprd_mif->end_y));           // start_y 0 end_y 239.
    }
#elif defined(CONFIG_ARCH_MESON2)
    if (blend_mtn_en) {
        WRITE_MPEG_REG(DI_MTNCRD_X, (di_mtncrd_mif->start_x << 16) | (di_mtncrd_mif->end_x));           // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNCRD_Y, (di_mtncrd_mif->start_y << 16) | (di_mtncrd_mif->end_y));           // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_MTNPRD_X, (di_mtnprd_mif->start_x << 16) | (di_mtnprd_mif->end_x));           // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNPRD_Y, (di_mtnprd_mif->start_y << 16) | (di_mtnprd_mif->end_y));           // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_MTNRD_CTRL, (di_mtnprd_mif->canvas_num << 8) |                                //mtnp canvas index.
                       ((prepost_link && di_vpp_en) << 16) |                                           // urgent
                       di_mtncrd_mif->canvas_num);                                                    // current field mtn canvas index.
    }
#endif

    if (di_ddr_en) {
        WRITE_MPEG_REG(DI_DIWR_X, (di_diwr_mif->start_x << 16) | (di_diwr_mif->end_x));                 // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_DIWR_Y, (di_diwr_mif->start_y << 16) | (di_diwr_mif->end_y * 2 + 1));        // start_y 0 end_y 479.
        WRITE_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num |                                          // canvas index.
                       (di_vpp_en << 8));                                                              // urgent.
    }

#if defined(CONFIG_ARCH_MESON)
    WRITE_MPEG_REG(DI_PRE_CTRL, nr_en |                         // NR enable
                   (mtn_en << 1) |                             // MTN_EN
                   (pd32_check_en << 2) |                      // check 3:2 pulldown
                   (pd22_check_en << 3) |                      // check 2:2 pulldown
                   (1 << 4) |                                  // 2:2 check mid pixel come from next field after MTN.
                   (hist_check_en << 5) |                      // hist check enable
                   (0 << 6) |                                  // hist check not use chan2.
                   ((!nr_en) << 7) |                           // hist check use data before noise reduction.
                   (pd22_check_en << 8) |                      // chan 2 enable for 2:2 pull down check.
                   (pd22_check_en << 9) |                      // line buffer 2 enable
                   (0 << 10) |                                 // pre drop first.
                   (0 << 11) |                                 // pre repeat.
                   (di_vpp_en << 12) |                         // pre viu link
                   (hold_line << 16) |                         // pre hold line number
                   (pre_field_num << 29) |                     // pre field number.
                   (0x1 << 30)                                 // pre soft rst, pre frame rst.
                  );

    WRITE_MPEG_REG(DI_POST_CTRL, ((ei_en || di_vpp_en || di_ddr_en) << 0) |                 // line buffer 0 enable
                   (buf1_en << 1)  |                                                     // line buffer 1 enable
                   (ei_en << 2) |                                                        // ei  enable
                   (blend_mtn_en << 3) |                                                 // mtn line buffer enable
                   ((blend_mtn_en && !prepost_link) << 4) |                              // mtnp read mif enable
                   (blend_en << 5) |                                                     // di blend enble.
                   (1 << 6) |                                                            // di mux output enable
                   (di_ddr_en << 7) |                                                    // di write to SDRAM enable.
                   (di_vpp_en << 8) |                                                    // di to VPP enable.
                   (0 << 9) |                                                            // mif0 to VPP enable.
                   (0 << 10) |                                                           // post drop first.
                   (0 << 11) |                                                           // post repeat.
                   (1 << 12) |                                                           // post viu link
                   (prepost_link << 13) |
                   (hold_line << 16) |                                                   // post hold line number
                   (post_field_num << 29) |                                              // post field number.
                   (0x1 << 30)                                                           // post soft rst  post frame rst.
                  );
#elif defined(CONFIG_ARCH_MESON2)
    WRITE_MPEG_REG(DI_PRE_CTRL, nr_en |                         // NR enable
                   (mtn_en << 1) |                             // MTN_EN
                   (pd32_check_en << 2) |                      // check 3:2 pulldown
                   (pd22_check_en << 3) |                      // check 2:2 pulldown
                   (nr_en << 4) |                              // 2:2 check mid pixel come from next field after MTN.
                   (hist_check_en << 5) |                      // hist check enable
                   (1 << 6) |                                  // hist check not use chan2.
                   ((!nr_en) << 7) |                           // hist check use data before noise reduction.
                   (pd22_check_en << 8) |                      // chan 2 enable for 2:2 pull down check.
                   (pd22_check_en << 9) |                      // line buffer 2 enable
                   (0 << 10) |                                 // pre drop first.
                   (0 << 11) |                                 // pre repeat.
                   (di_vpp_en << 12) |                         // pre viu link
                   (hold_line << 16) |                         // pre hold line number
                   (nr_en << 22) |                             // MTN after NR.
                   (pre_field_num << 29) |                     // pre field number.
                   (0x1 << 30)                                 // pre soft rst, pre frame rst.
                  );

    WRITE_MPEG_REG(DI_POST_CTRL, ((ei_en || blend_en) << 0) |                               // line buffer 0 enable
                   (buf1_en << 1)  |                                                     // line buffer 1 enable
                   (ei_en << 2) |                                                        // ei  enable
                   (blend_mtn_en << 3) |                                                 // mtn line buffer enable
                   (blend_mtn_en  << 4) |                                                // mtnp read mif enable
                   (blend_en << 5) |                                                     // di blend enble.
                   (1 << 6) |                                                            // di mux output enable
                   (di_ddr_en << 7) |                                                    // di write to SDRAM enable.
                   (di_vpp_en << 8) |                                                    // di to VPP enable.
                   (0 << 9) |                                                            // mif0 to VPP enable.
                   (0 << 10) |                                                           // post drop first.
                   (0 << 11) |                                                           // post repeat.
                   (di_vpp_en << 12) |                                                   // post viu link
                   (prepost_link << 13) |
                   (hold_line << 16) |                                                   // post hold line number
                   (post_field_num << 29) |                                              // post field number.
                   (0x1 << 30)                                                           // post soft rst  post frame rst.
                  );
#endif


    if (ei_only == 0) {
#if defined(CONFIG_ARCH_MESON)
        WRITE_MPEG_REG(DI_BLEND_CTRL, (READ_MPEG_REG(DI_BLEND_CTRL) & (~((1 << 25) | (3 << 20)))) |     // clean some bit we need to set.
                       (blend_mtn_en << 26) |                                                        // blend mtn enable.
                       (0 << 25) |                                                                   // blend with the mtn of the pre display field and next display field.
                       (1 << 24) |                                                                   // blend with pre display field.
                       (blend_mode << 20)                                                            // motion adaptive blend.
                      );
#elif defined(CONFIG_ARCH_MESON2)
        WRITE_MPEG_REG(DI_BLEND_CTRL, (post_mb_en << 28) |                                                   // post motion blur enable.
                       (0 << 27) |                                                                    // mtn3p(l, c, r) max.
                       (0 << 26) |                                                                    // mtn3p(l, c, r) min.
                       (0 << 25) |                                                                    // mtn3p(l, c, r) ave.
                       (1 << 24) |                                                                    // mtntopbot max
                       (blend_mtn_filt_en  << 23) |                                                   // blend mtn filter enable.
                       (blend_data_filt_en << 22) |                                                   // blend data filter enable.
                       (blend_mode << 20) |                                                       // motion adaptive blend.
                       25                                                                            // kdeint.
                      );
        WRITE_MPEG_REG(DI_BLEND_CTRL1, (196 << 24) |                                                        // char level
                       (64 << 16) |                                                                   // angle thredhold.
                       (40 << 8)  |                                                                   // all_af filt thd.
                       (64));                                                                         // all 4 equal
        WRITE_MPEG_REG(DI_BLEND_CTRL2, (4 << 8) |                                                           // mtn no mov level.
                       (48));                                                                        //black level.
#endif
    }
}

int di_mode_check(int cur_field)
{
    int i;

    WRITE_MPEG_REG(DI_INFO_ADDR, 0);
#if defined(CONFIG_ARCH_MESON)
    for (i  = 0; i <= 76; i++)
#elif defined(CONFIG_ARCH_MESON2)
    for (i  = 0; i <= 82; i++)
#endif
    {
        di_info[cur_field][i] = READ_MPEG_REG(DI_INFO_DATA);
    }

    WRITE_MPEG_REG(DI_PRE_CTRL, READ_MPEG_REG(DI_PRE_CTRL) | (0x1 << 30));               // pre soft rst
    WRITE_MPEG_REG(DI_POST_CTRL, READ_MPEG_REG(DI_POST_CTRL) | (0x1 << 30));             // post soft rst

    return (0);
}

void set_di_inp_fmt_more(int hfmt_en,
                         int hz_yc_ratio,        //2bit
                         int hz_ini_phase,       //4bit
                         int vfmt_en,
                         int vt_yc_ratio,        //2bit
                         int vt_ini_phase,       //4bit
                         int y_length,
                         int c_length,
                         int hz_rpt              //1bit
                        )
{
    int repeat_l0_en = 1, nrpt_phase0_en = 0;
    int vt_phase_step = (16 >> vt_yc_ratio);

    WRITE_MPEG_REG(DI_INP_FMT_CTRL,
                   (hz_rpt << 28)        |           //hz rpt pixel
                   (hz_ini_phase << 24)  |           //hz ini phase
                   (0 << 23)             |           //repeat p0 enable
                   (hz_yc_ratio << 21)   |           //hz yc ratio
                   (hfmt_en << 20)       |           //hz enable
                   (nrpt_phase0_en << 17) |          //nrpt_phase0 enable
                   (repeat_l0_en << 16)  |           //repeat l0 enable
                   (0 << 12)             |           //skip line num
                   (vt_ini_phase << 8)   |           //vt ini phase
                   (vt_phase_step << 1)  |           //vt phase step (3.4)
                   (vfmt_en << 0)                    //vt enable
                  );

    WRITE_MPEG_REG(DI_INP_FMT_W, (y_length << 16) |             //hz format width
                   (c_length << 0)                    //vt format width
                  );
}

void set_di_inp_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;
    unsigned long vt_ini_phase = 0;

    if (mif->set_separate_en == 1 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 1;
        chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;

        if (mif->output_field_num == 0) {
            vt_ini_phase = 0xe;
        } else {
            vt_ini_phase = 0xa;
        }
    } else if (mif->set_separate_en == 1 && mif->src_field_mode == 0) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x0;
        chroma0_rpt_loop_pat = 0x0;
    } else if (mif->set_separate_en == 0 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x00;
    } else {
        chro_rpt_lastl_ctrl = 0;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x00;
        chroma0_rpt_loop_pat = 0x00;
    }


    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    WRITE_MPEG_REG(DI_INP_GEN_REG, (urgent << 28)           |       // chroma urgent bit
                   (urgent << 27)              |       // luma urgent bit.
                   (1 << 25)                   |       // no dummy data.
                   (hold_line << 19)           |       // hold lines
                   (1 << 18)                   |       // push dummy pixel
                   (demux_mode << 16)          |       // demux_mode
                   (bytes_per_pixel << 14)     |
                   (mif->burst_size_cr << 12)  |
                   (mif->burst_size_cb << 10)  |
                   (mif->burst_size_y << 8)    |
                   (chro_rpt_lastl_ctrl << 6)  |
                   (mif->set_separate_en << 1) |
                   (1 << 0)                            // cntl_enable
                  );

    // ----------------------
    // Canvas
    // ----------------------
    WRITE_MPEG_REG(DI_INP_CANVAS0, (mif->canvas0_addr2 << 16)       |       // cntl_canvas0_addr2
                   (mif->canvas0_addr1 << 8)            |       // cntl_canvas0_addr1
                   (mif->canvas0_addr0 << 0)                    // cntl_canvas0_addr0
                  );

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    WRITE_MPEG_REG(DI_INP_LUMA_X0, (mif->luma_x_end0 << 16) |               // cntl_luma_x_end0
                   (mif->luma_x_start0 << 0)                    // cntl_luma_x_start0
                  );
    WRITE_MPEG_REG(DI_INP_LUMA_Y0, (mif->luma_y_end0 << 16) |               // cntl_luma_y_end0
                   (mif->luma_y_start0 << 0)                    // cntl_luma_y_start0
                  );
    WRITE_MPEG_REG(DI_INP_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                   (mif->chroma_x_start0 << 0)
                  );
    WRITE_MPEG_REG(DI_INP_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                   (mif->chroma_y_start0 << 0)
                  );

    // ----------------------
    // Repeat or skip
    // ----------------------
    WRITE_MPEG_REG(DI_INP_RPT_LOOP, (0 << 28) |
                   (0 << 24) |
                   (0 << 20) |
                   (0 << 16) |
                   (chroma0_rpt_loop_start << 12) |
                   (chroma0_rpt_loop_end << 8)  |
                   (luma0_rpt_loop_start << 4)  |
                   (luma0_rpt_loop_end << 0)
                  ) ;

    WRITE_MPEG_REG(DI_INP_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    WRITE_MPEG_REG(DI_INP_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    WRITE_MPEG_REG(DI_INP_DUMMY_PIXEL, 0x00808000);
    if ((mif->set_separate_en == 1)) {   // 4:2:0 block mode.
        set_di_inp_fmt_more(
            1,                                                  // hfmt_en
            1,                                                  // hz_yc_ratio
            0,                                                  // hz_ini_phase
            1,                                                  // vfmt_en
            1,                                                  // vt_yc_ratio
            vt_ini_phase,                                       // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,          // y_length
            mif->chroma_x_end0 - mif->chroma_x_start0 + 1 ,     // c length
            0);                                                 // hz repeat.
    } else {
        set_di_inp_fmt_more(
            1,                                                          // hfmt_en
            1,                                                          // hz_yc_ratio
            0,                                                          // hz_ini_phase
            0,                                                          // vfmt_en
            0,                                                          // vt_yc_ratio
            0,                                                          // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,                  // y_length
            ((mif->luma_x_end0 >> 1) - (mif->luma_x_start0 >> 1) + 1),  // c length
            0);                  // hz repeat.
    }
}

void set_di_mem_fmt_more(int hfmt_en,
                         int hz_yc_ratio,        //2bit
                         int hz_ini_phase,       //4bit
                         int vfmt_en,
                         int vt_yc_ratio,        //2bit
                         int vt_ini_phase,       //4bit
                         int y_length,
                         int c_length,
                         int hz_rpt              //1bit
                        )
{
    int vt_phase_step = (16 >> vt_yc_ratio);

    WRITE_MPEG_REG(DI_MEM_FMT_CTRL,
                   (hz_rpt << 28)        |           //hz rpt pixel
                   (hz_ini_phase << 24)  |           //hz ini phase
                   (0 << 23)             |           //repeat p0 enable
                   (hz_yc_ratio << 21)   |           //hz yc ratio
                   (hfmt_en << 20)       |           //hz enable
                   (1 << 17)             |           //nrpt_phase0 enable
                   (0 << 16)             |           //repeat l0 enable
                   (0 << 12)             |           //skip line num
                   (vt_ini_phase << 8)   |           //vt ini phase
                   (vt_phase_step << 1)  |           //vt phase step (3.4)
                   (vfmt_en << 0)                    //vt enable
                  );

    WRITE_MPEG_REG(DI_MEM_FMT_W, (y_length << 16) |         //hz format width
                   (c_length << 0)                    //vt format width
                  );
}

void set_di_mem_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;

    if (mif->set_separate_en == 1 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 1;
        chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;
    } else if (mif->set_separate_en == 1 && mif->src_field_mode == 0) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x0;
        chroma0_rpt_loop_pat = 0x0;
    } else if (mif->set_separate_en == 0 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x00;
    } else {
        chro_rpt_lastl_ctrl = 0;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x00;
        chroma0_rpt_loop_pat = 0x00;
    }

    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    WRITE_MPEG_REG(DI_MEM_GEN_REG,
                   (urgent << 28)              |       // urgent bit.
                   (urgent << 27)              |       // urgent bit.
                   (1 << 25)                   |       // no dummy data.
                   (hold_line << 19)           |       // hold lines
                   (1 << 18)                   |       // push dummy pixel
                   (demux_mode << 16)          |       // demux_mode
                   (bytes_per_pixel << 14)     |
                   (mif->burst_size_cr << 12)  |
                   (mif->burst_size_cb << 10)  |
                   (mif->burst_size_y << 8)    |
                   (chro_rpt_lastl_ctrl << 6)  |
                   (mif->set_separate_en << 1) |
                   (1 << 0)                            // cntl_enable
                  );

    // ----------------------
    // Canvas
    // ----------------------
    WRITE_MPEG_REG(DI_MEM_CANVAS0, (mif->canvas0_addr2 << 16)       |   // cntl_canvas0_addr2
                   (mif->canvas0_addr1 << 8)            |   // cntl_canvas0_addr1
                   (mif->canvas0_addr0 << 0)                // cntl_canvas0_addr0
                  );

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    WRITE_MPEG_REG(DI_MEM_LUMA_X0, (mif->luma_x_end0 << 16)         |   // cntl_luma_x_end0
                   (mif->luma_x_start0 << 0)                // cntl_luma_x_start0
                  );
    WRITE_MPEG_REG(DI_MEM_LUMA_Y0, (mif->luma_y_end0 << 16)         |   // cntl_luma_y_end0
                   (mif->luma_y_start0 << 0)                // cntl_luma_y_start0
                  );
    WRITE_MPEG_REG(DI_MEM_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                   (mif->chroma_x_start0 << 0)
                  );
    WRITE_MPEG_REG(DI_MEM_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                   (mif->chroma_y_start0 << 0)
                  );

    // ----------------------
    // Repeat or skip
    // ----------------------
    WRITE_MPEG_REG(DI_MEM_RPT_LOOP, (0 << 28) |
                   (0   << 24) |
                   (0   << 20) |
                   (0     << 16) |
                   (chroma0_rpt_loop_start << 12) |
                   (chroma0_rpt_loop_end << 8) |
                   (luma0_rpt_loop_start << 4) |
                   (luma0_rpt_loop_end << 0)
                  ) ;

    WRITE_MPEG_REG(DI_MEM_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    WRITE_MPEG_REG(DI_MEM_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    WRITE_MPEG_REG(DI_MEM_DUMMY_PIXEL, 0x00808000);
    if ((mif->set_separate_en == 1)) {  // 4:2:0 block mode.
        set_di_mem_fmt_more(
            1,                                                      // hfmt_en
            1,                                                      // hz_yc_ratio
            0,                                                      // hz_ini_phase
            1,                                                      // vfmt_en
            1,                                                      // vt_yc_ratio
            0,                                                      // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,              // y_length
            mif->chroma_x_end0 - mif->chroma_x_start0 + 1,          // c length
            0);                                                     // hz repeat.
    } else {
        set_di_mem_fmt_more(
            1,                                                          // hfmt_en
            1,                                                          // hz_yc_ratio
            0,                                                          // hz_ini_phase
            0,                                                          // vfmt_en
            0,                                                          // vt_yc_ratio
            0,                                                          // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,                  // y_length
            ((mif->luma_x_end0 >> 1) - (mif->luma_x_start0 >> 1) + 1),  // c length
            0);                                                         // hz repeat.
    }
}

void set_di_if1_fmt_more(int hfmt_en,
                         int hz_yc_ratio,        //2bit
                         int hz_ini_phase,       //4bit
                         int vfmt_en,
                         int vt_yc_ratio,        //2bit
                         int vt_ini_phase,       //4bit
                         int y_length,
                         int c_length,
                         int hz_rpt              //1bit
                        )
{
    int vt_phase_step = (16 >> vt_yc_ratio);

    WRITE_MPEG_REG(DI_IF1_FMT_CTRL,
                   (hz_rpt << 28)        |       //hz rpt pixel
                   (hz_ini_phase << 24)  |       //hz ini phase
                   (0 << 23)             |       //repeat p0 enable
                   (hz_yc_ratio << 21)   |       //hz yc ratio
                   (hfmt_en << 20)       |       //hz enable
                   (1 << 17)             |       //nrpt_phase0 enable
                   (0 << 16)             |       //repeat l0 enable
                   (0 << 12)             |       //skip line num
                   (vt_ini_phase << 8)   |       //vt ini phase
                   (vt_phase_step << 1)  |       //vt phase step (3.4)
                   (vfmt_en << 0)                //vt enable
                  );

    WRITE_MPEG_REG(DI_IF1_FMT_W, (y_length << 16) |         //hz format width
                   (c_length << 0)                //vt format width
                  );
}

void set_di_if1_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;

    if (mif->set_separate_en == 1 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 1;
        chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;
    } else if (mif->set_separate_en == 1 && mif->src_field_mode == 0) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x0;
        chroma0_rpt_loop_pat = 0x0;
    } else if (mif->set_separate_en == 0 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x00;
    } else {
        chro_rpt_lastl_ctrl = 0;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x00;
        chroma0_rpt_loop_pat = 0x00;
    }

    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    WRITE_MPEG_REG(DI_IF1_GEN_REG, (urgent << 28)           |   // urgent
                   (urgent << 27)              |   // luma urgent
                   (1 << 25)                   |   // no dummy data.
                   (hold_line << 19)           |   // hold lines
                   (1 << 18)                   |   // push dummy pixel
                   (demux_mode << 16)          |   // demux_mode
                   (bytes_per_pixel << 14)     |
                   (mif->burst_size_cr << 12)  |
                   (mif->burst_size_cb << 10)  |
                   (mif->burst_size_y << 8)    |
                   (chro_rpt_lastl_ctrl << 6)  |
                   (mif->set_separate_en << 1) |
                   (1 << 0)                        // cntl_enable
                  );

    // ----------------------
    // Canvas
    // ----------------------
    WRITE_MPEG_REG(DI_IF1_CANVAS0, (mif->canvas0_addr2 << 16)   |   // cntl_canvas0_addr2
                   (mif->canvas0_addr1 << 8)        |   // cntl_canvas0_addr1
                   (mif->canvas0_addr0 << 0)            // cntl_canvas0_addr0
                  );

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    WRITE_MPEG_REG(DI_IF1_LUMA_X0, (mif->luma_x_end0 << 16) |       // cntl_luma_x_end0
                   (mif->luma_x_start0 << 0)            // cntl_luma_x_start0
                  );
    WRITE_MPEG_REG(DI_IF1_LUMA_Y0, (mif->luma_y_end0 << 16) |       // cntl_luma_y_end0
                   (mif->luma_y_start0 << 0)            // cntl_luma_y_start0
                  );
    WRITE_MPEG_REG(DI_IF1_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                   (mif->chroma_x_start0 << 0)
                  );
    WRITE_MPEG_REG(DI_IF1_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                   (mif->chroma_y_start0 << 0)
                  );

    // ----------------------
    // Repeat or skip
    // ----------------------
    WRITE_MPEG_REG(DI_IF1_RPT_LOOP, (0 << 28)   |
                   (0   << 24)      |
                   (0   << 20)      |
                   (0     << 16)    |
                   (chroma0_rpt_loop_start << 12) |
                   (chroma0_rpt_loop_end << 8) |
                   (luma0_rpt_loop_start << 4) |
                   (luma0_rpt_loop_end << 0)
                  ) ;

    WRITE_MPEG_REG(DI_IF1_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    WRITE_MPEG_REG(DI_IF1_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    WRITE_MPEG_REG(DI_IF1_DUMMY_PIXEL, 0x00808000);
    if ((mif->set_separate_en == 1)) {  // 4:2:0 block mode.
        set_di_if1_fmt_more(
            1,                                                      // hfmt_en
            1,                                                      // hz_yc_ratio
            0,                                                      // hz_ini_phase
            1,                                                      // vfmt_en
            1,                                                      // vt_yc_ratio
            0,                                                      // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,              // y_length
            mif->chroma_x_end0 - mif->chroma_x_start0 + 1 ,         // c length
            0);                                                     // hz repeat.
    } else {
        set_di_if1_fmt_more(
            1,                                                          // hfmt_en
            1,                                                          // hz_yc_ratio
            0,                                                          // hz_ini_phase
            0,                                                          // vfmt_en
            0,                                                          // vt_yc_ratio
            0,                                                          // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,                  // y_length
            ((mif->luma_x_end0 >> 1) - (mif->luma_x_start0 >> 1) + 1),  // c length
            0);                  // hz repeat.
    }
}

void set_di_chan2_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;

    bytes_per_pixel = mif->set_separate_en ? 0 : ((mif->video_mode == 1) ? 2 : 1);
    demux_mode =  mif->video_mode & 1;

    if (mif->src_field_mode == 1) {
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
    } else {
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0;
    }
    // ----------------------
    // General register
    // ----------------------

    WRITE_MPEG_REG(DI_CHAN2_GEN_REG, (urgent << 28)                 |   // urgent
                   (urgent << 27)                      |   // luma urgent
                   (1 << 25)                           |   // no dummy data.
                   (hold_line << 19)                   |   // hold lines
                   (1 << 18)                           |   // push dummy pixel
                   (demux_mode << 16)                  |   // demux_mode
                   (bytes_per_pixel << 14)             |
                   (0 << 12)                           |
                   (0 << 10)                           |
                   (mif->burst_size_y << 8)            |
                   ((hold_line == 0 ? 1 : 0) << 7)   |      //manual start.
                   (0 << 6)                            |
                   (0 << 1)                            |
                   (1 << 0)                                // cntl_enable
                  );


    // ----------------------
    // Canvas
    // ----------------------
    WRITE_MPEG_REG(DI_CHAN2_CANVAS, (0 << 16)       |       // cntl_canvas0_addr2
                   (0 << 8)            |       // cntl_canvas0_addr1
                   (mif->canvas0_addr0 << 0)   // cntl_canvas0_addr0
                  );

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    WRITE_MPEG_REG(DI_CHAN2_LUMA_X, (mif->luma_x_end0 << 16)    |       // cntl_luma_x_end0
                   (mif->luma_x_start0 << 0)               // cntl_luma_x_start0
                  );
    WRITE_MPEG_REG(DI_CHAN2_LUMA_Y, (mif->luma_y_end0 << 16)  |         // cntl_luma_y_end0
                   (mif->luma_y_start0 << 0)               // cntl_luma_y_start0
                  );

    // ----------------------
    // Repeat or skip
    // ----------------------
    WRITE_MPEG_REG(DI_CHAN2_RPT_LOOP, (0 << 28) |
                   (0   << 24) |
                   (0   << 20) |
                   (0   << 16) |
                   (0   << 12) |
                   (0   << 8)  |
                   (luma0_rpt_loop_start << 4)  |
                   (luma0_rpt_loop_end << 0)
                  );

    WRITE_MPEG_REG(DI_CHAN2_LUMA_RPT_PAT, luma0_rpt_loop_pat);

    // Dummy pixel value
    WRITE_MPEG_REG(DI_CHAN2_DUMMY_PIXEL, 0x00808000);
}

void set_di_if0_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
    unsigned long bytes_per_pixel;
    unsigned long demux_mode;
    unsigned long chro_rpt_lastl_ctrl;
    unsigned long luma0_rpt_loop_start;
    unsigned long luma0_rpt_loop_end;
    unsigned long luma0_rpt_loop_pat;
    unsigned long chroma0_rpt_loop_start;
    unsigned long chroma0_rpt_loop_end;
    unsigned long chroma0_rpt_loop_pat;

    if (mif->set_separate_en == 1 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 1;
        chroma0_rpt_loop_end = 1;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x80;
    } else if (mif->set_separate_en == 1 && mif->src_field_mode == 0) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x0;
        chroma0_rpt_loop_pat = 0x0;
    } else if (mif->set_separate_en == 0 && mif->src_field_mode == 1) {
        chro_rpt_lastl_ctrl = 1;
        luma0_rpt_loop_start = 1;
        luma0_rpt_loop_end = 1;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x80;
        chroma0_rpt_loop_pat = 0x00;
    } else {
        chro_rpt_lastl_ctrl = 0;
        luma0_rpt_loop_start = 0;
        luma0_rpt_loop_end = 0;
        chroma0_rpt_loop_start = 0;
        chroma0_rpt_loop_end = 0;
        luma0_rpt_loop_pat = 0x00;
        chroma0_rpt_loop_pat = 0x00;
    }

    bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
    demux_mode = mif->video_mode;


    // ----------------------
    // General register
    // ----------------------

    WRITE_MPEG_REG(VD1_IF0_GEN_REG, (urgent << 28)          |       // urgent
                   (urgent << 27)              |       // luma urgent
                   (1 << 25)                   |       // no dummy data.
                   (hold_line << 19)           |       // hold lines
                   (1 << 18)                   |       // push dummy pixel
                   (demux_mode << 16)          |       // demux_mode
                   (bytes_per_pixel << 14)     |
                   (mif->burst_size_cr << 12)  |
                   (mif->burst_size_cb << 10)  |
                   (mif->burst_size_y << 8)    |
                   (chro_rpt_lastl_ctrl << 6)  |
                   (mif->set_separate_en << 1) |
                   (1 << 0)                            // cntl_enable
                  );

    // ----------------------
    // Canvas
    // ----------------------
    WRITE_MPEG_REG(VD1_IF0_CANVAS0, (mif->canvas0_addr2 << 16)      |   // cntl_canvas0_addr2
                   (mif->canvas0_addr1 << 8)            |   // cntl_canvas0_addr1
                   (mif->canvas0_addr0 << 0)                // cntl_canvas0_addr0
                  );

    // ----------------------
    // Picture 0 X/Y start,end
    // ----------------------
    WRITE_MPEG_REG(VD1_IF0_LUMA_X0, (mif->luma_x_end0 << 16) |      // cntl_luma_x_end0
                   (mif->luma_x_start0 << 0)            // cntl_luma_x_start0
                  );
    WRITE_MPEG_REG(VD1_IF0_LUMA_Y0, (mif->luma_y_end0 << 16) |      // cntl_luma_y_end0
                   (mif->luma_y_start0 << 0)            // cntl_luma_y_start0
                  );
    WRITE_MPEG_REG(VD1_IF0_CHROMA_X0, (mif->chroma_x_end0 << 16) |
                   (mif->chroma_x_start0 << 0)
                  );
    WRITE_MPEG_REG(VD1_IF0_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
                   (mif->chroma_y_start0 << 0)
                  );

    // ----------------------
    // Repeat or skip
    // ----------------------
    WRITE_MPEG_REG(VD1_IF0_RPT_LOOP, (0 << 28)      |
                   (0   << 24)          |
                   (0   << 20)          |
                   (0   << 16)          |
                   (chroma0_rpt_loop_start << 12) |
                   (chroma0_rpt_loop_end << 8) |
                   (luma0_rpt_loop_start << 4) |
                   (luma0_rpt_loop_end << 0)
                  ) ;

    WRITE_MPEG_REG(VD1_IF0_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
    WRITE_MPEG_REG(VD1_IF0_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

    // Dummy pixel value
    WRITE_MPEG_REG(VD1_IF0_DUMMY_PIXEL, 0x00808000);

    // ----------------------
    // Picture 1 unused
    // ----------------------
    WRITE_MPEG_REG(VD1_IF0_LUMA_X1, 0);                             // unused
    WRITE_MPEG_REG(VD1_IF0_LUMA_Y1, 0);                           // unused
    WRITE_MPEG_REG(VD1_IF0_CHROMA_X1, 0);                        // unused
    WRITE_MPEG_REG(VD1_IF0_CHROMA_Y1, 0);                        // unused
    WRITE_MPEG_REG(VD1_IF0_LUMA_PSEL, 0);                           // unused only one picture
    WRITE_MPEG_REG(VD1_IF0_CHROMA_PSEL, 0);                      // unused only one picture

    if ((mif->set_separate_en == 1)) {  // 4:2:0 block mode.
        set_vd1_fmt_more(
            1,                                                  // hfmt_en
            1,                                                  // hz_yc_ratio
            0,                                                  // hz_ini_phase
            1,                                                  // vfmt_en
            1,                                                  // vt_yc_ratio
            0,                                                  // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,          // y_length
            mif->chroma_x_end0 - mif->chroma_x_start0 + 1 ,     // c length
            0);                                                 // hz repeat.
    } else {
        set_vd1_fmt_more(
            1,                                                          // hfmt_en
            1,                                                          // hz_yc_ratio
            0,                                                          // hz_ini_phase
            0,                                                          // vfmt_en
            0,                                                          // vt_yc_ratio
            0,                                                          // vt_ini_phase
            mif->luma_x_end0 - mif->luma_x_start0 + 1,                  // y_length
            ((mif->luma_x_end0 >> 1) - (mif->luma_x_start0 >> 1) + 1) , //c length
            0);                                                         // hz repeat.
    }
}

//enable deinterlace pre module separated for pre post separate tests.
void enable_di_pre(
    DI_MIF_t        *di_inp_mif,
    DI_MIF_t        *di_mem_mif,
    DI_MIF_t        *di_chan2_mif,
    DI_SIM_MIF_t    *di_nrwr_mif,
    DI_SIM_MIF_t    *di_mtnwr_mif,
    int nr_en, int mtn_en, int pd32_check_en, int pd22_check_en, int hist_check_en,
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
    int nr_hfilt_en, int nr_hfilt_mb_en, int mtn_modify_en,
#endif
    int pre_field_num, int pre_viu_link, int hold_line)
{
    int hist_check_only;

#if defined(CONFIG_ARCH_MESON2)
    int nr_zone_0, nr_zone_1, nr_zone_2;

    if (noise_reduction_level == 0) {
        nr_zone_0 = 1;
        nr_zone_1 = 3;
        nr_zone_2 = 5;
    } else {
        nr_zone_0 = 3;
        nr_zone_1 = 6;
        nr_zone_2 = 10;
    }
#endif

    hist_check_only = hist_check_en && !nr_en && !mtn_en && !pd22_check_en && !pd32_check_en ;

    if (nr_en | mtn_en | pd22_check_en || pd32_check_en) {
        set_di_mem_mif(di_mem_mif, 0, hold_line);           // set urgent 0
        if (!vdin_en) {
            set_di_inp_mif(di_inp_mif, 0, hold_line);    // set urgent 0
        }
    }

    if (pd22_check_en || hist_check_only) {
        set_di_chan2_mif(di_chan2_mif, 0, hold_line);       // set urgent 0.
    }

    // set nr wr mif interface.
    if (nr_en) {
        WRITE_MPEG_REG(DI_NRWR_X, (di_nrwr_mif->start_x << 16) | (di_nrwr_mif->end_x));     // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_NRWR_Y, (di_nrwr_mif->start_y << 16) | (di_nrwr_mif->end_y));     // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_NRWR_CTRL, di_nrwr_mif->canvas_num);                              // canvas index.
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
        WRITE_MPEG_REG(DI_NR_CTRL0, (1 << 31) |                                             // nr yuv enable.
                       (1 << 30) |                                                         // nr range. 3 point
                       (0 << 29) |                                                         // max of 3 point.
                       (nr_hfilt_en << 28) |                                               // nr hfilter enable.
                       (nr_hfilt_mb_en << 27) |                                            // nr hfilter motion_blur enable.
                       (nr_zone_2 << 16) |                                                 // zone 2
                       (nr_zone_1 << 8) |                                                  // zone 1
                       (nr_zone_0 << 0));                                                  // zone 0
        WRITE_MPEG_REG(DI_NR_CTRL2, (10 << 24) |                                             //intra noise level
                       (1 << 16)  |                                                         // intra no noise level.
                       (10 << 8) |                                                          // inter noise level.
                       (1 << 0));                                                           // inter no noise level.
        WRITE_MPEG_REG(DI_NR_CTRL3, (16 << 16) |                                             // if any one of 3 point  mtn larger than 16 don't use 3 point.
                       720);                                                               // if one line eq cnt is larger than this number, this line is not conunted.
#endif
    }

    // motion wr mif.
    if (mtn_en) {
        WRITE_MPEG_REG(DI_MTNWR_X, (di_mtnwr_mif->start_x << 16) | (di_mtnwr_mif->end_x));      // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNWR_Y, (di_mtnwr_mif->start_y << 16) | (di_mtnwr_mif->end_y));      // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_MTNWR_CTRL, di_mtnwr_mif->canvas_num |                                // canvas index.
                       (0 << 8));                                                                // urgent.
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
        WRITE_MPEG_REG(DI_MTN_CTRL, (1 << 31) |                                                 // lpf enable.
                       (1 << 30) |                                                            // mtn uv enable.
                       (mtn_modify_en << 29) |                                                // no mtn modify.
                       (2 << 24) |                                                            // char diff count.
                       (40 << 16) |                                                           // black level.
                       (196 << 8) |                                                           // white level.
                       (64 << 0));                                                            // char diff level.
        WRITE_MPEG_REG(DI_MTN_CTRL1, (3 << 8) |                                                 // mtn shift if mtn modifty_en
                       0);                                                                   // mtn reduce before shift.
#endif
    }

    // reset pre
    WRITE_MPEG_REG(DI_PRE_CTRL, READ_MPEG_REG(DI_PRE_CTRL) |
                   1 << 31);                                        // frame reset for the pre modules.

#if defined(CONFIG_ARCH_MESON)
    WRITE_MPEG_REG(DI_PRE_CTRL, nr_en |                             // NR enable
                   (mtn_en << 1) |                                 // MTN_EN
                   (pd32_check_en << 2) |                          // check 3:2 pulldown
                   (pd22_check_en << 3) |                          // check 2:2 pulldown
                   (1 << 4) |                                      // 2:2 check mid pixel come from next field after MTN.
                   (hist_check_en << 5) |                          // hist check enable
                   (hist_check_only << 6) |                        // hist check  use chan2.
                   ((!nr_en) << 7) |                               // hist check use data before noise reduction.
                   ((pd22_check_en || hist_check_only) << 8) |     // chan 2 enable for 2:2 pull down check.
                   (pd22_check_en << 9) |                          // line buffer 2 enable
                   (0 << 10) |                                     // pre drop first.
                   (0 << 11) |                                     // pre repeat.
                   (0 << 12) |                                     // pre viu link
                   (hold_line << 16) |                             // pre hold line number
                   (pre_field_num << 29) |                         // pre field number.
                   (0x1 << 30)                                     // pre soft rst, pre frame rst.
                  );
#elif defined(CONFIG_ARCH_MESON2)
    WRITE_MPEG_REG(DI_PRE_CTRL, nr_en |                             // NR enable
                   (mtn_en << 1) |                                 // MTN_EN
                   (pd32_check_en << 2) |                          // check 3:2 pulldown
                   (pd22_check_en << 3) |                          // check 2:2 pulldown
                   (1 << 4) |                                      // 2:2 check mid pixel come from next field after MTN.
                   (hist_check_en << 5) |                          // hist check enable
                   (1 << 6) |                                      // hist check  use chan2.
                   ((!nr_en) << 7) |                               // hist check use data before noise reduction.
                   ((pd22_check_en || hist_check_only) << 8) |     // chan 2 enable for 2:2 pull down check.
                   (pd22_check_en << 9) |                          // line buffer 2 enable
                   (0 << 10) |                                     // pre drop first.
                   (0 << 11) |                                     // pre repeat.
                   (0 << 12) |                                     // pre viu link
                   (hold_line << 16) |                             // pre hold line number
                   (1 << 22) |                                     // MTN after NR.
                   (pre_field_num << 29) |                         // pre field number.
                   (0x1 << 30)                                     // pre soft rst, pre frame rst.
                  );
#endif
}

// enable di post module for separate test.
void enable_di_post(
    DI_MIF_t        *di_buf0_mif,
    DI_MIF_t        *di_buf1_mif,
    DI_SIM_MIF_t    *di_diwr_mif,
    DI_SIM_MIF_t    *di_mtncrd_mif,
    DI_SIM_MIF_t    *di_mtnprd_mif,
    int ei_en, int blend_en, int blend_mtn_en, int blend_mode, int di_vpp_en, int di_ddr_en,
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
    int blend_mtn_filt_en, int blend_data_filt_en, int post_mb_en,
#endif
    int post_field_num, int hold_line)
{
    int ei_only;
    int buf1_en;

    ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en);
    buf1_en = (!ei_only && (di_ddr_en || di_vpp_en));

    if (ei_en || di_vpp_en || di_ddr_en) {
        set_di_if0_mif(di_buf0_mif, di_vpp_en, hold_line);
    }

    if (!ei_only && (di_ddr_en || di_vpp_en)) {
        set_di_if1_mif(di_buf1_mif, di_vpp_en, hold_line);
    }

    // motion for current display field.
    if (blend_mtn_en) {
        WRITE_MPEG_REG(DI_MTNPRD_X, (di_mtnprd_mif->start_x << 16) | (di_mtnprd_mif->end_x));           // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNPRD_Y, (di_mtnprd_mif->start_y << 16) | (di_mtnprd_mif->end_y));           // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_MTNCRD_X, (di_mtncrd_mif->start_x << 16) | (di_mtncrd_mif->end_x));               // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_MTNCRD_Y, (di_mtncrd_mif->start_y << 16) | (di_mtncrd_mif->end_y));               // start_y 0 end_y 239.
        WRITE_MPEG_REG(DI_MTNRD_CTRL, (di_mtnprd_mif->canvas_num << 8) |                                    //mtnp canvas index.
                       (1 << 16) |                                                                         // urgent
                       di_mtncrd_mif->canvas_num);                                                        // current field mtn canvas index.
    }

    if (di_ddr_en) {
        WRITE_MPEG_REG(DI_DIWR_X, (di_diwr_mif->start_x << 16) | (di_diwr_mif->end_x));                  // start_x 0 end_x 719.
        WRITE_MPEG_REG(DI_DIWR_Y, (di_diwr_mif->start_y << 16) | (di_diwr_mif->end_y * 2 + 1));          // start_y 0 end_y 479.
        WRITE_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num |                                           // canvas index.
                       (di_vpp_en << 8));                                                              // urgent.
    }

    if (ei_only == 0) {
#if defined(CONFIG_ARCH_MESON)
        WRITE_MPEG_REG(DI_BLEND_CTRL, (READ_MPEG_REG(DI_BLEND_CTRL) & (~((1 << 25) | (3 << 20)))) |   // clean some bit we need to set.
                       (blend_mtn_en << 26) |                                                        // blend mtn enable.
                       (0 << 25) |                                                                   // blend with the mtn of the pre display field and next display field.
                       (1 << 24) |                                                                   // blend with pre display field.
                       (blend_mode << 20)                                                            // motion adaptive blend.
                      );
#elif defined(CONFIG_ARCH_MESON2)
        WRITE_MPEG_REG(DI_BLEND_CTRL, (post_mb_en << 28) |                                                   // post motion blur enable.
                       (0 << 27) |                                                                    // mtn3p(l, c, r) max.
                       (0 << 26) |                                                                    // mtn3p(l, c, r) min.
                       (0 << 25) |                                                                    // mtn3p(l, c, r) ave.
                       (1 << 24) |                                                                    // mtntopbot max
                       (blend_mtn_filt_en  << 23) |                                                   // blend mtn filter enable.
                       (blend_data_filt_en << 22) |                                                   // blend data filter enable.
                       (blend_mode << 20) |                                                       // motion adaptive blend.
                       25                                                                            // kdeint.
                      );
        WRITE_MPEG_REG(DI_BLEND_CTRL1, (196 << 24) |                                                        // char level
                       (64 << 16) |                                                                   // angle thredhold.
                       (40 << 8)  |                                                                   // all_af filt thd.
                       (64));                                                                         // all 4 equal
        WRITE_MPEG_REG(DI_BLEND_CTRL2, (4 << 8) |                                                           // mtn no mov level.
                       (48));                                                                        //black level.
#endif
    }

#if defined(CONFIG_ARCH_MESON)
    WRITE_MPEG_REG(DI_POST_CTRL, ((ei_en | blend_en) << 0) |        // line buffer 0 enable
                   (0 << 1)  |                                   // line buffer 1 enable
                   (ei_en << 2) |                                // ei  enable
                   (blend_mtn_en << 3) |                         // mtn line buffer enable
                   (blend_mtn_en  << 4) |                        // mtnp read mif enable
                   (blend_en << 5) |                             // di blend enble.
                   (1 << 6) |                                    // di mux output enable
                   (di_ddr_en << 7) |                            // di write to SDRAM enable.
                   (di_vpp_en << 8) |                            // di to VPP enable.
                   (0 << 9) |                                    // mif0 to VPP enable.
                   (0 << 10) |                                   // post drop first.
                   (0 << 11) |                                   // post repeat.
                   (1 << 12) |                                   // post viu link
                   (hold_line << 16) |                           // post hold line number
                   (post_field_num << 29) |                      // post field number.
                   (0x1 << 30)                                   // post soft rst  post frame rst.
                  );
#elif defined(CONFIG_ARCH_MESON2)
    WRITE_MPEG_REG(DI_POST_CTRL, ((ei_en | blend_en) << 0) |        // line buffer 0 enable
                   (0 << 1)  |                                   // line buffer 1 enable
                   (ei_en << 2) |                                // ei  enable
                   (blend_mtn_en << 3) |                         // mtn line buffer enable
                   (blend_mtn_en  << 4) |                        // mtnp read mif enable
                   (blend_en << 5) |                             // di blend enble.
                   (1 << 6) |                                    // di mux output enable
                   (di_ddr_en << 7) |                            // di write to SDRAM enable.
                   (di_vpp_en << 8) |                            // di to VPP enable.
                   (0 << 9) |                                    // mif0 to VPP enable.
                   (0 << 10) |                                   // post drop first.
                   (0 << 11) |                                   // post repeat.
                   (di_vpp_en << 12) |                           // post viu link
                   (hold_line << 16) |                           // post hold line number
                   (post_field_num << 29) |                      // post field number.
                   (0x1 << 30)                                   // post soft rst  post frame rst.
                  );
#endif
}

int di_pre_mode_check(int cur_field)
{
    int i;

    WRITE_MPEG_REG(DI_INFO_ADDR, 0);
    for (i  = 0; i <= 68; i++) {
        di_info[cur_field][i] = READ_MPEG_REG(DI_INFO_DATA);
    }

#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
    WRITE_MPEG_REG(DI_INFO_ADDR, 77);
    for (i  = 77; i <= 82; i++) {
        di_info[cur_field][i] = READ_MPEG_REG(DI_INFO_DATA);
    }
#endif

    return (0);
}

int di_post_mode_check(int cur_field)
{
    int i;

    WRITE_MPEG_REG(DI_INFO_ADDR, 69);
    for (i  = 69; i <= 76; i++) {
        di_info[cur_field][i] = READ_MPEG_REG(DI_INFO_DATA);
    }

    return (0);
}

void enable_region_blend(
    int reg0_en, int reg0_start_x, int reg0_end_x, int reg0_start_y, int reg0_end_y, int reg0_mode,
    int reg1_en, int reg1_start_x, int reg1_end_x, int reg1_start_y, int reg1_end_y, int reg1_mode,
    int reg2_en, int reg2_start_x, int reg2_end_x, int reg2_start_y, int reg2_end_y, int reg2_mode,
    int reg3_en, int reg3_start_x, int reg3_end_x, int reg3_start_y, int reg3_end_y, int reg3_mode)
{
    WRITE_MPEG_REG(DI_BLEND_REG0_X, (reg0_start_x << 16) |
                   reg0_end_x);
    WRITE_MPEG_REG(DI_BLEND_REG0_Y, (reg0_start_y << 16) |
                   reg0_end_y);
    WRITE_MPEG_REG(DI_BLEND_REG1_X, (reg1_start_x << 16) |
                   reg1_end_x);
    WRITE_MPEG_REG(DI_BLEND_REG1_Y, (reg1_start_y << 16) |
                   reg1_end_y);
    WRITE_MPEG_REG(DI_BLEND_REG2_X, (reg2_start_x << 16) |
                   reg2_end_x);
    WRITE_MPEG_REG(DI_BLEND_REG2_Y, (reg2_start_y << 16) |
                   reg2_end_y);
    WRITE_MPEG_REG(DI_BLEND_REG3_X, (reg3_start_x << 16) |
                   reg3_end_x);
    WRITE_MPEG_REG(DI_BLEND_REG3_Y, (reg3_start_y << 16) |
                   reg3_end_y);
    WRITE_MPEG_REG(DI_BLEND_CTRL, (READ_MPEG_REG(DI_BLEND_CTRL) & (~(0xfff << 8))) |
                   (reg0_mode << 8)   |
                   (reg1_mode << 10)  |
                   (reg2_mode << 12)  |
                   (reg3_mode << 14)  |
                   (reg0_en << 16)      |
                   (reg1_en << 17)      |
                   (reg2_en << 18)      |
                   (reg3_en << 19));
}

int check_p32_p22(int cur_field, int pre_field, int pre2_field)
{
    unsigned int cur_data, pre_data, pre2_data;
    unsigned int cur_num, pre_num, pre2_num;
    unsigned int data_diff, num_diff;

    di_p22_info = di_p22_info << 1;
    cur_data = di_info[cur_field][2];
    pre_data = di_info[pre_field][2];
    pre2_data = di_info[pre2_field][2];
    cur_num = di_info[cur_field][4] & 0xffffff;
    pre_num = di_info[pre_field][4] & 0xffffff;
    pre2_num = di_info[pre2_field][4] & 0xffffff;

    if (cur_data * 2 <= pre_data && pre2_data * 2 <= pre_data && cur_num * 2 <= pre_num && pre2_num * 2 <= pre_num) {
        di_p22_info |= 1;
    }

    di_p32_info = di_p32_info << 1;
    di_p32_info_2 = di_p32_info_2 << 1;
    di_p22_info_2 = di_p22_info_2 << 1;
    cur_data = di_info[cur_field][0];
    cur_num = di_info[cur_field][1] & 0xffffff;
    pre_data = di_info[pre_field][0];
    pre_num = di_info[pre_field][1] & 0xffffff;

    data_diff = cur_data > pre_data ? cur_data - pre_data : pre_data - cur_data;
    num_diff = cur_num > pre_num ? cur_num - pre_num : pre_num - cur_num;

    if ((di_p22_info & 0x1) && data_diff * 10 <= cur_data && num_diff * 10 <= cur_num) {
        di_p22_info_2 |= 1;
    }

    if (di_p32_counter > 0 || di_p32_info == 0) {
        if (cur_data * 2 <= pre_data && cur_num * 50 <= pre_num) {
            di_p32_info |= 1;
            last_big_data = pre_data;
            last_big_num = pre_num;
            di_p32_counter = -1;
        } else {
            last_big_data = 0;
            last_big_num = 0;

            if ((di_p32_counter & 0x1) && data_diff * 5 <= cur_data && num_diff * 5 <= cur_num) {
                di_p32_info_2 |= 1;
            }
        }
    } else {
        if (cur_data * 2 <= last_big_data && cur_num * 50 <= last_big_num) {
            di_p32_info |= 1;
            di_p32_counter = -1;
        }
    }

    di_p32_counter++;

    return 0;
}

void pattern_check_prepost(void)
{
    if (pre_field_counter != di_checked_field) {
        di_checked_field = pre_field_counter;
        di_mode_check(pre_field_counter % 4);

#ifdef DEBUG
        debug_array[(pre_field_counter & 0x3ff) * 4] = di_info[pre_field_counter % 4][0];
        debug_array[(pre_field_counter & 0x3ff) * 4 + 1] = di_info[pre_field_counter % 4][1] & 0xffffff;
        debug_array[(pre_field_counter & 0x3ff) * 4 + 2] = di_info[pre_field_counter % 4][2];
        debug_array[(pre_field_counter & 0x3ff) * 4 + 3] = di_info[pre_field_counter % 4][4];
#endif

        if (pre_field_counter >= 3) {
            check_p32_p22(pre_field_counter % 4, (pre_field_counter + 3) % 4, (pre_field_counter + 2) % 4);

#if defined(CONFIG_ARCH_MESON)
            pattern_22 = pattern_22 << 1;
            if (di_info[pre_field_counter % 4][4] < di_info[(pre_field_counter + 3) % 4][4]) {
                pattern_22 |= 1;
            }
#endif
        }
    }

    di_chan2_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 3) % 4;
    di_mem_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 2) % 4;
    blend_mode = 3;

#if defined(CONFIG_ARCH_MESON)
    di_buf0_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 3) % 4;

    // 2:2 check
    if (((di_p22_info & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK))
        && ((di_p22_info_2 & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK))) {
        blend_mode = 1;
    } else if (((di_p22_info & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK))
               && ((di_p22_info_2 & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK))) {
        blend_mode = 0;
    }
#elif defined(CONFIG_ARCH_MESON2)
    di_buf1_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 1) % 4;

    if (((di_p22_info & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK))
        && ((di_p22_info_2 & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK))) {
        di_buf1_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 3) % 4;
        blend_mode = 1;
    } else if (((di_p22_info & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK))
               && ((di_p22_info_2 & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK))) {
        di_buf1_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 1) % 4;
        blend_mode = 0;
    }
#endif

    // pull down pattern check
    if (pattern_len == 0) {
        int i, j, pattern, pattern_2, mask;

        for (j = 5 ; j < 22 ; j++) {
            mask = (1 << j) - 1;
            pattern = di_p32_info & mask;
            pattern_2 = di_p32_info_2 & mask;

            if (pattern != 0 && pattern_2 != 0 && pattern != mask) {
                for (i = j ; i < j * 3 ; i += j)
                    if (((di_p32_info >> i) & mask) != pattern || ((di_p32_info_2 >> i) & mask) != pattern_2) {
                        break;
                    }

                if (i == j * 3) {
#if defined(CONFIG_ARCH_MESON)
                    if (pattern_22 & (1 << (j - 1))) {
                        blend_mode = 1;
                    } else {
                        blend_mode = 0;
                    }
#elif defined(CONFIG_ARCH_MESON2)
                    if (di_info[(field_counter + 3) % 4][4] < di_info[(field_counter + 2) % 4][4]) {
                        di_buf1_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 3) % 4;
                        blend_mode = 1;
                    } else {
                        di_buf1_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 1) % 4;
                        blend_mode = 0;
                    }
#endif

                    pattern_len = j;
                    break;
                }
            }
        }
    } else {
        int i, pattern, pattern_2, mask;

        mask = (1 << pattern_len) - 1;
        pattern = di_p32_info & mask;
        pattern_2 = di_p32_info_2 & mask;

        for (i = pattern_len ; i < pattern_len * 3 ; i += pattern_len)
            if (((di_p32_info >> i) & mask) != pattern || ((di_p32_info_2 >> i) & mask) != pattern_2) {
                break;
            }

        if (i == pattern_len * 3) {
#if defined(CONFIG_ARCH_MESON)
            if (pattern_22 & (1 << (pattern_len - 1))) {
                blend_mode = 1;
            } else {
                blend_mode = 0;
            }
#elif defined(CONFIG_ARCH_MESON2)
            if (di_info[(field_counter + 3) % 4][4] < di_info[(field_counter + 2) % 4][4]) {
                di_buf1_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 3) % 4;
                blend_mode = 1;
            } else {
                di_buf1_mif.canvas0_addr0 = DEINTERLACE_CANVAS_BASE_INDEX + (field_counter + 1) % 4;
                blend_mode = 0;
            }
#endif
        } else {
            pattern_len = 0;
        }
    }

    di_nrwr_mif.canvas_num = DEINTERLACE_CANVAS_BASE_INDEX + field_counter % 4;
    di_mtnwr_mif.canvas_num = DEINTERLACE_CANVAS_BASE_INDEX + 4 + field_counter % 4;
    di_mtncrd_mif.canvas_num = DEINTERLACE_CANVAS_BASE_INDEX + 4 + (field_counter + 2) % 4;
    di_mtnprd_mif.canvas_num = DEINTERLACE_CANVAS_BASE_INDEX + 4 + (field_counter + 3) % 4;
}

void pattern_check_pre(void)
{
    di_pre_mode_check(pre_field_counter % 4);

#ifdef DEBUG
    debug_array[(pre_field_counter & 0x3ff) * 4] = di_info[pre_field_counter % 4][0];
    debug_array[(pre_field_counter & 0x3ff) * 4 + 1] = di_info[pre_field_counter % 4][1] & 0xffffff;
    debug_array[(pre_field_counter & 0x3ff) * 4 + 2] = di_info[pre_field_counter % 4][2];
    debug_array[(pre_field_counter & 0x3ff) * 4 + 3] = di_info[pre_field_counter % 4][4];
#endif

    if (pre_field_counter >= 3) {
        check_p32_p22(pre_field_counter % 4, (pre_field_counter - 1) % 4, (pre_field_counter - 2) % 4);

        if (di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode == 3) {
            if (((di_p22_info & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK))
                && ((di_p22_info_2 & PATTERN22_MARK) == (0x5555555555555555LL & PATTERN22_MARK))) {
                di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 1;
            } else if (((di_p22_info & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK))
                       && ((di_p22_info_2 & PATTERN22_MARK) == (0xaaaaaaaaaaaaaaaaLL & PATTERN22_MARK))) {
                di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 0;
            } else if (pattern_len == 0) {
                di_buf_pool[(pre_field_counter - 2) % DI_BUF_NUM].blend_mode = 3;
            }

            if (pattern_len == 0) {
                int i, j, pattern, pattern_2, mask;

                for (j = 5 ; j < 22 ; j++) {
                    mask = (1 << j) - 1;
                    pattern = di_p32_info & mask;
                    pattern_2 = di_p32_info_2 & mask;

                    if (pattern != 0 && pattern_2 != 0 && pattern != mask) {
                        for (i = j ; i < j * PATTERN32_NUM ; i += j)
                            if (((di_p32_info >> i) & mask) != pattern || ((di_p32_info_2 >> i) & mask) != pattern_2) {
                                break;
                            }

                        if (i == j * PATTERN32_NUM) {
                            if ((pattern_len == 5) && ((pattern & (pattern - 1)) == 0)) {
                                if ((di_p32_info & 0x1) || (di_p32_info & 0x2) || (di_p32_info & 0x8)) {
                                    di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 0;
                                } else {
                                    di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 1;
                                }
                            } else {
                                if ((pattern & (pattern - 1)) != 0) {
                                    if (di_info[pre_field_counter % 4][4] < di_info[(pre_field_counter - 1) % 4][4]) {
                                        di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 1;
                                    } else {
                                        di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 0;
                                    }
                                }
                            }

                            pattern_len = j;
                            break;
                        }
                    }
                }
            } else {
                int i, pattern, pattern_2, mask;

                mask = (1 << pattern_len) - 1;
                pattern = di_p32_info & mask;
                pattern_2 = di_p32_info_2 & mask;

                for (i = pattern_len ; i < pattern_len * PATTERN32_NUM ; i += pattern_len)
                    if (((di_p32_info >> i) & mask) != pattern || ((di_p32_info_2 >> i) & mask) != pattern_2) {
                        break;
                    }

                if (i == pattern_len * PATTERN32_NUM) {
                    if ((pattern_len == 5) && ((pattern & (pattern - 1)) == 0)) {
                        if ((di_p32_info & 0x1) || (di_p32_info & 0x2) || (di_p32_info & 0x8)) {
                            di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 0;
                        } else {
                            di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 1;
                        }
                    } else {
                        if ((pattern & (pattern - 1)) != 0) {
                            if (di_info[pre_field_counter % 4][4] < di_info[(pre_field_counter - 1) % 4][4]) {
                                di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 1;
                            } else {
                                di_buf_pool[(pre_field_counter - 1) % DI_BUF_NUM].blend_mode = 0;
                            }
                        }
                    }
                } else {
                    pattern_len = 0;
                    di_buf_pool[(pre_field_counter - 2) % DI_BUF_NUM].blend_mode = 3;
                }
            }
        }
    }
}

void set_vdin_par(int flag, vframe_t *buf)
{
    vdin_en = flag;
    memcpy(&dummy_buf, buf, sizeof(vframe_t));
}

void di_pre_process(void)
{
    unsigned temp = READ_MPEG_REG(DI_INTR_CTRL);
    unsigned status = READ_MPEG_REG(DI_PRE_CTRL) & 0x2;

#if defined(CONFIG_ARCH_MESON2)
    int nr_hfilt_en, nr_hfilt_mb_en;

    if (noise_reduction_level == 2) {
        nr_hfilt_en = 1;
        nr_hfilt_mb_en = 1;
    } else {
        nr_hfilt_en = 0;
        nr_hfilt_mb_en = 0;
    }
#endif

    if (deinterlace_mode != 2) {
        return;
    }

    if ((prev_struct == 0) && (READ_MPEG_REG(DI_PRE_SIZE) != ((32 - 1) | ((64 - 1) << 16)))) {
        disable_pre_deinterlace();
    }

    if (prev_struct > 0) {
#if defined(CONFIG_ARCH_MESON)
        if ((temp & 0xf) != (status | 0x9))
#elif defined(CONFIG_ARCH_MESON2)
        if ((temp & 0xf) != (status | 0x1))
#endif
            return;

        if (!vdin_en && (prog_field_count == 0) && (buf_recycle_done == 0)) {
            buf_recycle_done = 1;
            vf_put(cur_buf, RECEIVER_NAME);
        }

        if (di_pre_post_done == 0) {
            di_pre_post_done = 1;
            pattern_check_pre();

            memcpy((&di_buf_pool[pre_field_counter % DI_BUF_NUM]), cur_buf, sizeof(vframe_t));
            di_buf_pool[pre_field_counter % DI_BUF_NUM].blend_mode = blend_mode;
            di_buf_pool[pre_field_counter % DI_BUF_NUM].canvas0Addr = DEINTERLACE_CANVAS_BASE_INDEX + 4;
            di_buf_pool[pre_field_counter % DI_BUF_NUM].canvas1Addr = DEINTERLACE_CANVAS_BASE_INDEX + 4;

            if (prev_struct == 1) {
                di_buf_pool[pre_field_counter % DI_BUF_NUM].type = VIDTYPE_INTERLACE_TOP | VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
            } else {
                di_buf_pool[pre_field_counter % DI_BUF_NUM].type = VIDTYPE_INTERLACE_BOTTOM | VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
            }

            pre_field_counter++;
        }

        if ((pre_field_counter >= field_counter + DI_BUF_NUM - 3) && ((pre_field_counter >= field_counter + DI_BUF_NUM - 2) || (field_counter == 0))) {
#ifdef DEBUG
            di_pre_overflow++;
#endif
            return;
        }

        if (!vdin_en && (prog_field_count == 0) && (!vf_peek(RECEIVER_NAME))) {
#ifdef DEBUG
            di_pre_underflow++;
#endif
            return;
        }
    }

    if (prog_field_count > 0) {
        blend_mode = 0;
        prog_field_count--;
        prev_struct = 3 - prev_struct;
    } else {
        if (vdin_en) {
            di_pre_recycle_buf = 1;
            cur_buf = &dummy_buf;
        } else {
            cur_buf = vf_peek(RECEIVER_NAME);
            if (!cur_buf) {
                return;
            }

            if ((cur_buf->duration == 0)
#if defined(CONFIG_AM_DEINTERLACE_SD_ONLY)
                || (cur_buf->width > 720)
#endif
               ) {
                di_pre_recycle_buf = 0;
                return;
            }

            di_pre_recycle_buf = 1;
            cur_buf = vf_get(RECEIVER_NAME);
        }

        if (((cur_buf->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP && prev_struct == 1)
            || ((cur_buf->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_BOTTOM && prev_struct == 2)) {
            if (!vdin_en) {
                vf_put(cur_buf, RECEIVER_NAME);
            }
            return;
        }

        di_inp_top_mif.canvas0_addr0 = di_inp_bot_mif.canvas0_addr0 = cur_buf->canvas0Addr & 0xff;
        di_inp_top_mif.canvas0_addr1 = di_inp_bot_mif.canvas0_addr1 = (cur_buf->canvas0Addr >> 8) & 0xff;
        di_inp_top_mif.canvas0_addr2 = di_inp_bot_mif.canvas0_addr2 = (cur_buf->canvas0Addr >> 16) & 0xff;
        blend_mode = 3;

        if ((cur_buf->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP) {
            prev_struct = 1;
            prog_field_count = 0;
        } else if ((cur_buf->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_BOTTOM) {
            prev_struct = 2;
            prog_field_count = 0;
        } else {
            if (prev_struct == 0) {
                prev_struct = 1;
            } else {
                prev_struct = 3 - prev_struct;
            }

            if (cur_buf->duration_pulldown > 0) {
                prog_field_count = 2;
            } else {
                prog_field_count = 1;
            }

            blend_mode = 1;
            cur_buf->duration >>= 1;
            cur_buf->duration_pulldown = 0;
        }
    }

    buf_recycle_done = 0;
    di_pre_post_done = 0;
    WRITE_MPEG_REG(DI_INTR_CTRL, temp);

    if ((READ_MPEG_REG(DI_PRE_SIZE) != ((cur_buf->width - 1) | ((cur_buf->height / 2 - 1) << 16)))) {
        WRITE_MPEG_REG(DI_INTR_CTRL, 0x000f000f);
        initial_di_pre(cur_buf->width, cur_buf->height / 2, PRE_HOLD_LINE);

        di_checked_field = (field_counter + di_checked_field + 1) % DI_BUF_NUM;
        pre_field_counter = field_counter = 0;
        di_p32_info = di_p22_info = di_p32_info_2 = di_p22_info_2 = 0;
        pattern_len = 0;

        di_mem_mif.luma_x_start0    = 0;
        di_mem_mif.luma_x_end0      = cur_buf->width - 1;
        di_mem_mif.luma_y_start0    = 0;
        di_mem_mif.luma_y_end0      = cur_buf->height / 2 - 1;

        di_chan2_mif.luma_x_start0  = 0;
        di_chan2_mif.luma_x_end0    = cur_buf->width - 1;
        di_chan2_mif.luma_y_start0  = 0;
        di_chan2_mif.luma_y_end0    = cur_buf->height / 2 - 1;

        di_nrwr_mif.start_x         = 0;
        di_nrwr_mif.end_x           = cur_buf->width - 1;
        di_nrwr_mif.start_y         = 0;
        di_nrwr_mif.end_y           = cur_buf->height / 2 - 1;

        di_mtnwr_mif.start_x        = 0;
        di_mtnwr_mif.end_x          = cur_buf->width - 1;
        di_mtnwr_mif.start_y        = 0;
        di_mtnwr_mif.end_y          = cur_buf->height / 2 - 1;

        if (cur_buf->type & VIDTYPE_VIU_422) {
            di_inp_top_mif.video_mode = 0;
            di_inp_top_mif.set_separate_en = 0;
            di_inp_top_mif.src_field_mode = 0;
            di_inp_top_mif.output_field_num = 0;
            di_inp_top_mif.burst_size_y = 3;
            di_inp_top_mif.burst_size_cb = 0;
            di_inp_top_mif.burst_size_cr = 0;

            memcpy(&di_inp_bot_mif, &di_inp_top_mif, sizeof(DI_MIF_t));

            di_inp_top_mif.luma_x_start0    = 0;
            di_inp_top_mif.luma_x_end0      = cur_buf->width - 1;
            di_inp_top_mif.luma_y_start0    = 0;
            di_inp_top_mif.luma_y_end0      = cur_buf->height / 2 - 1;
            di_inp_top_mif.chroma_x_start0  = 0;
            di_inp_top_mif.chroma_x_end0    = 0;
            di_inp_top_mif.chroma_y_start0  = 0;
            di_inp_top_mif.chroma_y_end0    = 0;

            di_inp_bot_mif.luma_x_start0    = 0;
            di_inp_bot_mif.luma_x_end0      = cur_buf->width - 1;
            di_inp_bot_mif.luma_y_start0    = 0;
            di_inp_bot_mif.luma_y_end0      = cur_buf->height / 2 - 1;
            di_inp_bot_mif.chroma_x_start0  = 0;
            di_inp_bot_mif.chroma_x_end0    = 0;
            di_inp_bot_mif.chroma_y_start0  = 0;
            di_inp_bot_mif.chroma_y_end0    = 0;
        } else {
            di_inp_top_mif.video_mode = 0;
            di_inp_top_mif.set_separate_en = 1;
            di_inp_top_mif.src_field_mode = 1;
            di_inp_top_mif.burst_size_y = 3;
            di_inp_top_mif.burst_size_cb = 1;
            di_inp_top_mif.burst_size_cr = 1;

            memcpy(&di_inp_bot_mif, &di_inp_top_mif, sizeof(DI_MIF_t));

            di_inp_top_mif.output_field_num = 0;                                        // top
            di_inp_bot_mif.output_field_num = 1;                                        // bottom

            di_inp_top_mif.luma_x_start0    = 0;
            di_inp_top_mif.luma_x_end0      = cur_buf->width - 1;
            di_inp_top_mif.luma_y_start0    = 0;
            di_inp_top_mif.luma_y_end0      = cur_buf->height - 2;
            di_inp_top_mif.chroma_x_start0  = 0;
            di_inp_top_mif.chroma_x_end0    = cur_buf->width / 2 - 1;
            di_inp_top_mif.chroma_y_start0  = 0;
            di_inp_top_mif.chroma_y_end0    = cur_buf->height / 2 - 2;

            di_inp_bot_mif.luma_x_start0    = 0;
            di_inp_bot_mif.luma_x_end0      = cur_buf->width - 1;
            di_inp_bot_mif.luma_y_start0    = 1;
            di_inp_bot_mif.luma_y_end0      = cur_buf->height - 1;
            di_inp_bot_mif.chroma_x_start0  = 0;
            di_inp_bot_mif.chroma_x_end0    = cur_buf->width / 2 - 1;
            di_inp_bot_mif.chroma_y_start0  = 1;
            di_inp_bot_mif.chroma_y_end0    = cur_buf->height / 2 - 1;
        }

        di_nrwr_mif.canvas_num          = DEINTERLACE_CANVAS_BASE_INDEX;
        di_mtnwr_mif.canvas_num         = DEINTERLACE_CANVAS_BASE_INDEX + 1;
        di_chan2_mif.canvas0_addr0      = DEINTERLACE_CANVAS_BASE_INDEX + 2;
        di_mem_mif.canvas0_addr0        = DEINTERLACE_CANVAS_BASE_INDEX + 3;
        di_buf0_mif.canvas0_addr0       = DEINTERLACE_CANVAS_BASE_INDEX + 4;
        di_buf1_mif.canvas0_addr0       = DEINTERLACE_CANVAS_BASE_INDEX + 5;
        di_mtncrd_mif.canvas_num        = DEINTERLACE_CANVAS_BASE_INDEX + 6;
        di_mtnprd_mif.canvas_num        = DEINTERLACE_CANVAS_BASE_INDEX + 7;

        enable_di_mode_check(
            0, cur_buf->width - 1, 0, cur_buf->height / 2 - 1,                  // window 0 ( start_x, end_x, start_y, end_y)
            0, cur_buf->width - 1, 0, cur_buf->height / 2 - 1,                  // window 1 ( start_x, end_x, start_y, end_y)
            0, cur_buf->width - 1, 0, cur_buf->height / 2 - 1,                  // window 2 ( start_x, end_x, start_y, end_y)
            0, cur_buf->width - 1, 0, cur_buf->height / 2 - 1,                  // window 3 ( start_x, end_x, start_y, end_y)
            0, cur_buf->width - 1, 0, cur_buf->height / 2 - 1,                  // window 4 ( start_x, end_x, start_y, end_y)
            16, 16, 16, 16, 16,                                                 // windows 32 level
            256, 256, 256, 256, 256,                                            // windows 22 level
            16, 256);                                                           // field 32 level; field 22 level
    }

    temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((pre_field_counter + di_checked_field) % DI_BUF_NUM);
    canvas_config(di_nrwr_mif.canvas_num, temp, MAX_CANVAS_WIDTH * 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
    temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((pre_field_counter + di_checked_field) % DI_BUF_NUM) + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT);
    canvas_config(di_mtnwr_mif.canvas_num, temp, MAX_CANVAS_WIDTH / 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
    temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((pre_field_counter + di_checked_field + DI_BUF_NUM - 1) % DI_BUF_NUM);
    canvas_config(di_chan2_mif.canvas0_addr0, temp, MAX_CANVAS_WIDTH * 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
    temp = di_mem_start + (MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT * 5 / 4) * ((pre_field_counter + di_checked_field + DI_BUF_NUM - 2) % DI_BUF_NUM);
    canvas_config(di_mem_mif.canvas0_addr0, temp, MAX_CANVAS_WIDTH * 2, MAX_CANVAS_HEIGHT / 2, 0, 0);

    WRITE_MPEG_REG(DI_PRE_CTRL, 0x3 << 30);
    enable_di_pre(
        (prev_struct == 1) ? &di_inp_top_mif : &di_inp_bot_mif,
        (pre_field_counter < 2) ? ((prev_struct == 1) ? &di_inp_top_mif : &di_inp_bot_mif) : &di_mem_mif,
            &di_chan2_mif,
            &di_nrwr_mif,
            &di_mtnwr_mif,
            1,                                                                  // nr enable
            (pre_field_counter >= 2),                                           // mtn enable
            (pre_field_counter >= 2),                                           // 3:2 pulldown check enable
            (pre_field_counter >= 1),                                           // 2:2 pulldown check enable
#if defined(CONFIG_ARCH_MESON)
            1,                                                                  // hist check_en
#elif defined(CONFIG_ARCH_MESON2)
            0,                                                                  // hist check_en
            nr_hfilt_en,                                                        // nr_hfilt_en
            nr_hfilt_mb_en,                                                     // nr_hfilt_mb_en
            1,                                                                  // mtn_modify_en,
#endif
            (prev_struct == 1) ? 1 : 0,                                         // field num for chan2. 1 bottom, 0 top.
            0,                                                                  // pre viu link.
            PRE_HOLD_LINE
        );
}

void di_pre_isr(struct work_struct *work)
{

    if (!vf_get_provider(RECEIVER_NAME)) {
        return;
    }

    di_pre_process();
}

void run_deinterlace(unsigned zoom_start_x_lines, unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines,
                     unsigned type, int mode, int hold_line)
{
    int di_width, di_height, di_start_x, di_end_x, di_start_y, di_end_y, size_change, position_change;

#if defined(CONFIG_ARCH_MESON2)
    int nr_hfilt_en, nr_hfilt_mb_en, post_mb_en;

    if (noise_reduction_level == 2) {
        nr_hfilt_en = 1;
        nr_hfilt_mb_en = 1;
        post_mb_en = 1;
    } else {
        nr_hfilt_en = 0;
        nr_hfilt_mb_en = 0;
        post_mb_en = 0;
    }
#endif

    di_start_x = zoom_start_x_lines;
    di_end_x = zoom_end_x_lines;
    di_width = di_end_x - di_start_x + 1;
    di_start_y = (zoom_start_y_lines + 1) & 0xfffffffe;
    di_end_y = (zoom_end_y_lines - 1) | 0x1;
    di_height = di_end_y - di_start_y + 1;

    if (deinterlace_mode == 1) {
        int i;
        unsigned long addr = di_mem_start;

        size_change = (READ_MPEG_REG(DI_POST_SIZE) != ((di_width - 1) | ((di_height - 1) << 16)));
        position_change = ((di_inp_top_mif.luma_x_start0 != di_start_x) || (di_inp_top_mif.luma_y_start0 != di_start_y));

        if (size_change || position_change) {
            if (size_change) {
                initial_di_prepost(di_width, di_height / 2, di_width, di_height, hold_line);
                pattern_22 = 0;

                di_p32_info = di_p22_info = di_p32_info_2 = di_p22_info_2 = 0;
                pattern_len = 0;
            }

            di_mem_mif.luma_x_start0    = di_start_x;
            di_mem_mif.luma_x_end0      = di_end_x;
            di_mem_mif.luma_y_start0    = di_start_y / 2;
            di_mem_mif.luma_y_end0      = (di_end_y + 1) / 2 - 1;

            di_buf0_mif.luma_x_start0   = di_start_x;
            di_buf0_mif.luma_x_end0     = di_end_x;
            di_buf0_mif.luma_y_start0   = di_start_y / 2;
            di_buf0_mif.luma_y_end0     = (di_end_y + 1) / 2 - 1;

            di_chan2_mif.luma_x_start0  = di_start_x;
            di_chan2_mif.luma_x_end0    = di_end_x;
            di_chan2_mif.luma_y_start0  = di_start_y / 2;
            di_chan2_mif.luma_y_end0    = (di_end_y + 1) / 2 - 1;

            di_nrwr_mif.start_x         = di_start_x;
            di_nrwr_mif.end_x           = di_end_x;
            di_nrwr_mif.start_y         = di_start_y / 2;
            di_nrwr_mif.end_y           = (di_end_y + 1) / 2 - 1;

            di_mtnwr_mif.start_x        = di_start_x;
            di_mtnwr_mif.end_x          = di_end_x;
            di_mtnwr_mif.start_y        = di_start_y / 2;
            di_mtnwr_mif.end_y          = (di_end_y + 1) / 2 - 1;

            di_mtncrd_mif.start_x       = di_start_x;
            di_mtncrd_mif.end_x         = di_end_x;
            di_mtncrd_mif.start_y       = di_start_y / 2;
            di_mtncrd_mif.end_y         = (di_end_y + 1) / 2 - 1;

            enable_di_mode_check(
                di_start_x, di_end_x, di_start_y, (di_end_y + 1) / 2 - 1,       // window 0 ( start_x, end_x, start_y, end_y)
                di_start_x, di_end_x, di_start_y, (di_end_y + 1) / 2 - 1,       // window 1 ( start_x, end_x, start_y, end_y)
                di_start_x, di_end_x, di_start_y, (di_end_y + 1) / 2 - 1,       // window 2 ( start_x, end_x, start_y, end_y)
                di_start_x, di_end_x, di_start_y, (di_end_y + 1) / 2 - 1,       // window 3 ( start_x, end_x, start_y, end_y)
                di_start_x, di_end_x, di_start_y, (di_end_y + 1) / 2 - 1,       // window 4 ( start_x, end_x, start_y, end_y)
                16, 16, 16, 16, 16,                                             // windows 32 level
                256, 256, 256, 256, 256,                                        // windows 22 level
                16, 256);                                                       // field 32 level; field 22 level

            pre_field_counter = field_counter = di_checked_field = 0;

            if (type & VIDTYPE_VIU_422) {
                di_inp_top_mif.video_mode = 0;
                di_inp_top_mif.set_separate_en = 0;
                di_inp_top_mif.src_field_mode = 0;
                di_inp_top_mif.output_field_num = 0;
                di_inp_top_mif.burst_size_y = 3;
                di_inp_top_mif.burst_size_cb = 0;
                di_inp_top_mif.burst_size_cr = 0;

                memcpy(&di_inp_bot_mif, &di_inp_top_mif, sizeof(DI_MIF_t));

                di_inp_top_mif.luma_x_start0    = di_start_x;
                di_inp_top_mif.luma_x_end0      = di_end_x;
                di_inp_top_mif.luma_y_start0    = di_start_y;
                di_inp_top_mif.luma_y_end0      = (di_end_y + 1) / 2 - 1;
                di_inp_top_mif.chroma_x_start0  = 0;
                di_inp_top_mif.chroma_x_end0    = 0;
                di_inp_top_mif.chroma_y_start0  = 0;
                di_inp_top_mif.chroma_y_end0    = 0;

                di_inp_bot_mif.luma_x_start0    = di_start_x;
                di_inp_bot_mif.luma_x_end0      = di_end_x;
                di_inp_bot_mif.luma_y_start0    = di_start_y;
                di_inp_bot_mif.luma_y_end0      = (di_end_y + 1) / 2 - 1;
                di_inp_bot_mif.chroma_x_start0  = 0;
                di_inp_bot_mif.chroma_x_end0    = 0;
                di_inp_bot_mif.chroma_y_start0  = 0;
                di_inp_bot_mif.chroma_y_end0    = 0;
            } else {
                di_inp_top_mif.video_mode = 0;
                di_inp_top_mif.set_separate_en = 1;
                di_inp_top_mif.src_field_mode = 1;
                di_inp_top_mif.burst_size_y = 3;
                di_inp_top_mif.burst_size_cb = 1;
                di_inp_top_mif.burst_size_cr = 1;

                memcpy(&di_inp_bot_mif, &di_inp_top_mif, sizeof(DI_MIF_t));

                di_inp_top_mif.output_field_num = 0;                                        // top
                di_inp_bot_mif.output_field_num = 1;                                        // bottom

                di_inp_top_mif.luma_x_start0    = di_start_x;
                di_inp_top_mif.luma_x_end0      = di_end_x;
                di_inp_top_mif.luma_y_start0    = di_start_y;
                di_inp_top_mif.luma_y_end0      = di_end_y - 1;
                di_inp_top_mif.chroma_x_start0  = di_start_x / 2;
                di_inp_top_mif.chroma_x_end0    = (di_end_x + 1) / 2 - 1;
                di_inp_top_mif.chroma_y_start0  = di_start_y / 2;
                di_inp_top_mif.chroma_y_end0    = (di_end_y + 1) / 2 - 2;

                di_inp_bot_mif.luma_x_start0    = di_start_x;
                di_inp_bot_mif.luma_x_end0      = di_end_x;
                di_inp_bot_mif.luma_y_start0    = di_start_y + 1;
                di_inp_bot_mif.luma_y_end0      = di_end_y;
                di_inp_bot_mif.chroma_x_start0  = di_start_x / 2;
                di_inp_bot_mif.chroma_x_end0    = (di_end_x + 1) / 2 - 1;
                di_inp_bot_mif.chroma_y_start0  = di_start_y / 2 + 1;
                di_inp_bot_mif.chroma_y_end0    = (di_end_y + 1) / 2 - 1;
            }

            for (i = 0 ; i < 4 ; i++) {
                canvas_config(DEINTERLACE_CANVAS_BASE_INDEX + i, addr, MAX_CANVAS_WIDTH * 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
                addr += MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT;
            }

            for (i = 4 ; i < 8 ; i++) {
                canvas_config(DEINTERLACE_CANVAS_BASE_INDEX + i, addr, MAX_CANVAS_WIDTH / 2, MAX_CANVAS_HEIGHT / 2, 0, 0);
                addr += MAX_CANVAS_WIDTH * MAX_CANVAS_HEIGHT / 4;
            }

            di_inp_top_mif.canvas0_addr0 = di_inp_bot_mif.canvas0_addr0 = DISPLAY_CANVAS_BASE_INDEX;
            di_inp_top_mif.canvas0_addr1 = di_inp_bot_mif.canvas0_addr1 = DISPLAY_CANVAS_BASE_INDEX + 1;
            di_inp_top_mif.canvas0_addr2 = di_inp_bot_mif.canvas0_addr2 = DISPLAY_CANVAS_BASE_INDEX + 2;
        }

        pattern_check_prepost();

#if defined(CONFIG_ARCH_MESON)
        if ((type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP) {
            enable_di_prepost_full(
                &di_inp_top_mif,
                &di_mem_mif,
                (field_counter < 1 ? &di_inp_top_mif : &di_buf0_mif),
                NULL,
                &di_chan2_mif,
                &di_nrwr_mif,
                NULL,
                &di_mtnwr_mif,
                &di_mtncrd_mif,
                NULL,
                (pre_field_counter != field_counter || field_counter == 0),     // noise reduction enable
                (field_counter >= 2),                                           // motion check enable
                (pre_field_counter != field_counter && field_counter >= 2),     // 3:2 pulldown check enable
                (pre_field_counter != field_counter && field_counter >= 1),     // 2:2 pulldown check enable
                (pre_field_counter != field_counter || field_counter == 0),     // video luma histogram check enable
                1,                                                              // edge interpolation module enable.
                (field_counter >= 2),                                           // blend enable.
                (field_counter >= 2),                                           // blend with mtn.
                (field_counter < 2 ? 2 : blend_mode),                           // blend mode
                1,                                                              // deinterlace output to VPP.
                0,                                                              // deinterlace output to DDR SDRAM at same time.
                (field_counter >= 1),                                           // 1 = current display field is bottom field, we need generated top field.
                (field_counter >= 1),                                           // pre field num: 1 = current chan2 input field is bottom field.
                (field_counter >= 1),                                           // prepost link.  for the first field it look no need to be propost_link.
                hold_line
            );
        } else {
            enable_di_prepost_full(
                &di_inp_bot_mif,
                &di_mem_mif,
                (field_counter < 1 ? &di_inp_bot_mif : &di_buf0_mif),
                NULL,
                &di_chan2_mif,
                &di_nrwr_mif,
                NULL,
                &di_mtnwr_mif,
                &di_mtncrd_mif,
                NULL,
                (pre_field_counter != field_counter || field_counter == 0),     // noise reduction enable
                (field_counter >= 2),                                           // motion check enable
                (pre_field_counter != field_counter && field_counter >= 2),     // 3:2 pulldown check enable
                (pre_field_counter != field_counter && field_counter >= 1),     // 2:2 pulldown check enable
                (pre_field_counter != field_counter || field_counter == 0),     // video luma histogram check enable
                1,                                                              // edge interpolation module enable.
                (field_counter >= 2),                                           // blend enable.
                (field_counter >= 2),                                           // blend with mtn.
                (field_counter < 2 ? 2 : blend_mode),                           // blend mode: 3 motion adapative blend.
                1,                                                              // deinterlace output to VPP.
                0,                                                              // deinterlace output to DDR SDRAM at same time.
                (field_counter < 1),                                            // 1 = current display field is bottom field, we need generated top field.
                (field_counter < 1),                                            // pre field num.  1 = current chan2 input field is bottom field.
                (field_counter >= 1),                                           // prepost link.  for the first field it look no need to be propost_link.
                hold_line
            );
        }
#elif defined(CONFIG_ARCH_MESON2)
        if ((type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP) {
            enable_di_prepost_full(
                &di_inp_top_mif,
                (field_counter < 2 ? &di_inp_top_mif : &di_mem_mif),
                NULL,
                &di_buf1_mif,
                &di_chan2_mif,
                &di_nrwr_mif,
                NULL,
                &di_mtnwr_mif,
                &di_mtncrd_mif,
                &di_mtnprd_mif,
                (pre_field_counter != field_counter || field_counter == 0),     // noise reduction enable
                (field_counter >= 2),                                           // motion check enable
                (pre_field_counter != field_counter && field_counter >= 2),     // 3:2 pulldown check enable
                (pre_field_counter != field_counter && field_counter >= 1),     // 2:2 pulldown check enable
                (pre_field_counter != field_counter || field_counter == 0),     // video luma histogram check enable
                1,                                                              // edge interpolation module enable.
                (field_counter >= 3),                                           // blend enable.
                (field_counter >= 3),                                           // blend with mtn.
                (field_counter < 3 ? 2 : blend_mode),                           // blend mode
                1,                                                              // deinterlace output to VPP.
                0,                                                              // deinterlace output to DDR SDRAM at same time.
                nr_hfilt_en,                                                    // nr_hfilt_en
                nr_hfilt_mb_en,                                                 // nr_hfilt_mb_en
                1,                                                              // mtn_modify_en,
                1,                                                              // blend_mtn_filt_en
                1,                                                              // blend_data_filt_en
                post_mb_en,                                                     // post_mb_en
                0,                                                              // 1 = current display field is bottom field, we need generated top field.
                1,                                                              // pre field num: 1 = current chan2 input field is bottom field.
                (field_counter >= 2),                                           // prepost link.  for the first field it look no need to be propost_link.
                hold_line
            );
        } else {
            enable_di_prepost_full(
                &di_inp_bot_mif,
                (field_counter < 2 ? &di_inp_top_mif : &di_mem_mif),
                NULL,
                &di_buf1_mif,
                &di_chan2_mif,
                &di_nrwr_mif,
                NULL,
                &di_mtnwr_mif,
                &di_mtncrd_mif,
                &di_mtnprd_mif,
                (pre_field_counter != field_counter || field_counter == 0),     // noise reduction enable
                (field_counter >= 2),                                           // motion check enable
                (pre_field_counter != field_counter && field_counter >= 2),     // 3:2 pulldown check enable
                (pre_field_counter != field_counter && field_counter >= 1),     // 2:2 pulldown check enable
                (pre_field_counter != field_counter || field_counter == 0),     // video luma histogram check enable
                1,                                                              // edge interpolation module enable.
                (field_counter >= 3),                                           // blend enable.
                (field_counter >= 3),                                           // blend with mtn.
                (field_counter < 3 ? 2 : blend_mode),                           // blend mode: 3 motion adapative blend.
                1,                                                              // deinterlace output to VPP.
                0,                                                              // deinterlace output to DDR SDRAM at same time.
                nr_hfilt_en,                                                    // nr_hfilt_en
                nr_hfilt_mb_en,                                                 // nr_hfilt_mb_en
                1,                                                              // mtn_modify_en,
                1,                                                              // blend_mtn_filt_en
                1,                                                              // blend_data_filt_en
                post_mb_en,                                                     // post_mb_en
                1,                                                              // 1 = current display field is bottom field, we need generated top field.
                0,                                                              // pre field num.  1 = current chan2 input field is bottom field.
                (field_counter >= 2),                                           // prepost link.  for the first field it look no need to be propost_link.
                hold_line
            );
        }
#endif

        pre_field_counter = field_counter;
    } else {
        int post_blend_en, post_blend_mode;

        if (READ_MPEG_REG(DI_POST_SIZE) != ((di_width - 1) | ((di_height - 1) << 16))
            || (di_buf0_mif.luma_x_start0 != di_start_x) || (di_buf0_mif.luma_y_start0 != di_start_y / 2)) {
            initial_di_post(di_width, di_height, hold_line);

            di_buf0_mif.luma_x_start0   = di_start_x;
            di_buf0_mif.luma_x_end0     = di_end_x;
            di_buf0_mif.luma_y_start0   = di_start_y / 2;
            di_buf0_mif.luma_y_end0     = (di_end_y + 1) / 2 - 1;
            di_buf1_mif.luma_x_start0   = di_start_x;
            di_buf1_mif.luma_x_end0     = di_end_x;
            di_buf1_mif.luma_y_start0   = di_start_y / 2;
            di_buf1_mif.luma_y_end0     = (di_end_y + 1) / 2 - 1;
            di_mtncrd_mif.start_x       = di_start_x;
            di_mtncrd_mif.end_x         = di_end_x;
            di_mtncrd_mif.start_y       = di_start_y / 2;
            di_mtncrd_mif.end_y         = (di_end_y + 1) / 2 - 1;
            di_mtnprd_mif.start_x       = di_start_x;
            di_mtnprd_mif.end_x         = di_end_x;
            di_mtnprd_mif.start_y       = di_start_y / 2;
            di_mtnprd_mif.end_y         = (di_end_y + 1) / 2 - 1;
        }

        post_blend_en = 1;
        post_blend_mode = mode;

        if ((post_blend_mode == 3) && (field_counter <= 2)) {
            post_blend_en = 0;
            post_blend_mode = 2;
        }

        enable_di_post(
            &di_buf0_mif,
            &di_buf1_mif,
            NULL,
            &di_mtncrd_mif,
            &di_mtnprd_mif,
            1,                                                              // ei enable
            post_blend_en,                                                  // blend enable
            post_blend_en,                                                  // blend mtn enable
            post_blend_mode,                                                // blend mode.
            1,                                                              // di_vpp_en.
            0,                                                              // di_ddr_en.
#if defined(CONFIG_ARCH_MESON)
#elif defined(CONFIG_ARCH_MESON2)
            1,                                                              // blend_mtn_filt_en
            1,                                                              // blend_data_filt_en
            post_mb_en,                                                     // post_mb_en
#endif
            (type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP ? 0 : 1,     // 1 bottom generate top
            hold_line
        );
    }
}

void di_pre_timer_func(unsigned long arg)
{
    struct timer_list *timer = (struct timer_list *)arg;

    schedule_work(&di_pre_work);

    timer->expires = jiffies + DI_PRE_INTERVAL;
    add_timer(timer);
}

void deinterlace_init(void)
{
    di_mem_mif.chroma_x_start0 = 0;
    di_mem_mif.chroma_x_end0 =  0;
    di_mem_mif.chroma_y_start0 = 0;
    di_mem_mif.chroma_y_end0 =  0;
    di_mem_mif.video_mode = 0;
    di_mem_mif.set_separate_en = 0;
    di_mem_mif.src_field_mode = 0;
    di_mem_mif.output_field_num = 0;
    di_mem_mif.burst_size_y = 3;
    di_mem_mif.burst_size_cb = 0;
    di_mem_mif.burst_size_cr = 0;
    di_mem_mif.canvas0_addr1 = 0;
    di_mem_mif.canvas0_addr2 = 0;

    memcpy(&di_buf0_mif, &di_mem_mif, sizeof(DI_MIF_t));
    memcpy(&di_buf1_mif, &di_mem_mif, sizeof(DI_MIF_t));
    memcpy(&di_chan2_mif, &di_buf1_mif, sizeof(DI_MIF_t));

    WRITE_MPEG_REG(DI_PRE_HOLD, (1 << 31) | (31 << 16) | 31);

#if defined(CONFIG_ARCH_MESON)
    WRITE_MPEG_REG(DI_NRMTN_CTRL0, 0xb00a0603);
#endif

    INIT_WORK(&di_pre_work, di_pre_isr);

    init_timer(&di_pre_timer);
    di_pre_timer.data = (ulong) & di_pre_timer;
    di_pre_timer.function = di_pre_timer_func;
    di_pre_timer.expires = jiffies + DI_PRE_INTERVAL;
    add_timer(&di_pre_timer);
}

static int deinterlace_probe(struct platform_device *pdev)
{
    struct resource *mem;

    printk("Amlogic deinterlace init\n");

    if (!(mem = platform_get_resource(pdev, IORESOURCE_MEM, 0))) {
        printk("\ndeinterlace memory resource undefined.\n");
        return -EFAULT;
    }

    // declare deinterlace memory
    di_mem_start = mem->start;
    printk("Deinterlace memory: start = 0x%x, end = 0x%x\n", di_mem_start, mem->end);

    deinterlace_init();

    return 0;
}

static int deinterlace_remove(struct platform_device *pdev)
{
    printk("Amlogic deinterlace release\n");
    del_timer_sync(&di_pre_timer);
    return 0;
}

static struct platform_driver
        deinterlace_driver = {
    .probe      = deinterlace_probe,
    .remove     = deinterlace_remove,
    .driver     = {
        .name   = "deinterlace",
    }
};

static int __init deinterlace_module_init(void)
{
    if (platform_driver_register(&deinterlace_driver)) {
        printk("failed to register deinterlace module\n");
        return -ENODEV;
    }

    return 0;
}

static void __exit deinterlace_module_exit(void)
{
    platform_driver_unregister(&deinterlace_driver);
    return;
}

MODULE_PARM_DESC(deinterlace_mode, "\n deinterlace mode \n");
module_param(deinterlace_mode, int, 0664);
#if defined(CONFIG_ARCH_MESON2)
MODULE_PARM_DESC(noise_reduction_level, "\n noise reduction level \n");
module_param(noise_reduction_level, int, 0664);
#endif
module_init(deinterlace_module_init);
module_exit(deinterlace_module_exit);

MODULE_DESCRIPTION("AMLOGIC deinterlace driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Qi Wang <qi.wang@amlogic.com>");

