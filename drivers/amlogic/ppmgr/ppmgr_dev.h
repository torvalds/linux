#ifndef PPMGR_DEV_INCLUDE_H
#define PPMGR_DEV_INCLUDE_H
#include <linux/amlogic/amports/vframe.h>
typedef  struct {
	struct class 		*cla;
	struct device		*dev;
	char  			name[20];
	unsigned int 		open_count;
	int	 			major;
	unsigned  int 		dbg_enable;
	char* buffer_start;
	unsigned int buffer_size;

	unsigned angle;
	unsigned orientation;
	unsigned videoangle;

	int bypass;
	int disp_width;
	int disp_height;
	int canvas_width;
	int canvas_height;
#ifdef CONFIG_POST_PROCESS_MANAGER_PPSCALER
	int ppscaler_flag;
	int scale_h_start;
	int scale_h_end;
	int scale_v_start;
	int scale_v_end;
#endif
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
	unsigned ppmgr_3d_mode;
	unsigned direction_3d;
	int viewmode;
	unsigned scale_down;
#endif
	const vinfo_t *vinfo;
	int left;
	int top;
	int width;
	int height;
	int receiver;
	int receiver_format;
	int display_mode;
	int mirror_flag;
	int use_prot;
	int disable_prot;
	int started;
	int global_angle;
}ppmgr_device_t;

typedef struct ppframe_s {
    vframe_t  frame;
    int       index;
    int       angle;
    vframe_t  *dec_frame;
} ppframe_t;

#define to_ppframe(vf)	\
	container_of(vf, struct ppframe_s, frame)

extern ppmgr_device_t  ppmgr_device;
#endif /* PPMGR_DEV_INCLUDE_H. */
