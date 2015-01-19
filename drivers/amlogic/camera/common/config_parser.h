#ifndef CONFIG_PARSER
#define CONFIG_PARSER

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>

#define EFFECT_ITEM_MAX 16
#define AET_ITEM_MAX 32
#define HW_ITEM_MAX 16
#define WB_ITEM_MAX 10
#define CAPTURE_ITEM_MAX 5
#define NR_ITEM_MAX 5
#define PEAKING_ITEM_MAX 5
#define LENS_ITEM_MAX 5
#define SCENE_ITEM_MAX 1
#define EFFECT_MAX 18
#define HW_MAX 64
#define WB_MAX 2
#define GAMMA_MAX 257
#define SCENE_MAX 281
#define WB_SENSOR_MAX 4
#define CAPTURE_MAX 8
#define LENS_MAX 1027
#define WAVE_MAX 12
#define CM_MAX 188
#define NR_MAX 15
#define PEAKING_MAX 35
#define AE_LEN 119
#define AWB_LEN 120
#define AF_LEN	42
#define BUFFER_SIZE 1024

enum error_code {
	NO_MEM = 1,
	READ_ERROR,
	WRONG_FORMAT,
	CHECK_LEN_FAILED,
	CHECK_FAILED,
	HEAD_FAILED,
	BODY_HEAD_FAILED,
	BODY_ELEMENT_FAILED,
};

typedef struct{
    int num;
    char name[40];
    unsigned int export[EFFECT_MAX];
}effect_type;

typedef struct{
    int sum;
    effect_type eff[EFFECT_ITEM_MAX];
}effect_struct;

typedef struct{
    int num;
    char name[40];
    int export[HW_MAX];
}hw_type;

typedef struct{
    int sum;
    hw_type hw[HW_ITEM_MAX];
}hw_struct;

typedef struct{
    int num;
    char name[40];
    int export[2];
}wb_type;

typedef struct{
    int sum;
    wb_type wb[WB_ITEM_MAX];
}wb_struct;


typedef struct{
    int num;
    char name[40];
    int export[SCENE_MAX];
}scene_type;

typedef struct{
    int sum;
    scene_type scene[SCENE_ITEM_MAX];
}scene_struct;

typedef struct{
	int num;
	char name[40];
	int export[CAPTURE_ITEM_MAX];	
}capture_type;

typedef struct{
	int sum;
	capture_type capture[CAPTURE_MAX];
}capture_struct;

typedef struct sensor_aet_s {
    unsigned int exp;
    unsigned int ag;
    unsigned int vts;
    unsigned int gain;
    unsigned int fr;
} sensor_aet_t;

typedef struct sensor_aet_info_s {
    unsigned int fmt_main_fr;
    unsigned int fmt_capture; // false: preview, true: capture
    unsigned int fmt_hactive;
    unsigned int fmt_vactive;
    unsigned int fmt_rated_fr;
    unsigned int fmt_min_fr;
    unsigned int tbl_max_step;
    unsigned int tbl_rated_step;
    unsigned int tbl_max_gain;
    unsigned int tbl_min_gain;
    unsigned int format_transfer_parameter;
} sensor_aet_info_t;


typedef struct{
    int num;
    char name[40];
    sensor_aet_info_t *info;
    sensor_aet_t *aet_table;
}aet_type;

typedef struct{
    int sum;
    aet_type aet[AET_ITEM_MAX];
}aet_struct;

typedef struct{
	int export[WAVE_MAX];	
}wave_struct;

typedef struct{
	int num;
	char name[40];
	int export[LENS_MAX];
}lens_type;

typedef struct{
	int sum;
	lens_type lens[LENS_ITEM_MAX];	
}lens_struct;

typedef struct{
	unsigned short gamma_r[GAMMA_MAX];
	unsigned short gamma_g[GAMMA_MAX];
	unsigned short gamma_b[GAMMA_MAX];
}gamma_struct;

typedef struct{
    int export[WB_SENSOR_MAX];
}wb_sensor_struct;

typedef struct{
    char date[40];
    char module[30];
    char version[30];		
}version_struct;

typedef struct{
	int export[CM_MAX];	
}cm_struct;

typedef struct{
	int num;
	char name[40];
	int export[NR_MAX];	
}nr_type;

typedef struct{
	int sum;
	nr_type nr[NR_ITEM_MAX];	
}nr_struct;

typedef struct{
	int num;
	char name[40];
	int export[PEAKING_MAX];	
}peaking_type;

typedef struct{
	int sum;
	peaking_type peaking[PEAKING_ITEM_MAX];
}peaking_struct;

typedef struct{
    effect_struct eff;
    int effect_valid;
    hw_struct hw;
    int hw_valid;
    aet_struct aet; 
    int aet_valid;
    capture_struct capture;
    int capture_valid;
    scene_struct scene;
    int scene_valid;
    wb_struct wb;
    int wb_valid;
    wave_struct wave;
    int wave_valid;
    lens_struct lens;
    int lens_valid;
    gamma_struct gamma;
    int gamma_valid;
    wb_sensor_struct wb_sensor_data;
    int wb_sensor_data_valid;
    version_struct version;
    int version_info_valid;
    cm_struct cm;
    int cm_valid;
    nr_struct nr;
    int nr_valid;
    peaking_struct peaking;
    int peaking_valid;
}configure_t;

typedef struct{
	unsigned int effect_index;
	unsigned int scenes_index;
	unsigned int wb_index;
	unsigned int capture_index;
	unsigned int nr_index;
	unsigned int peaking_index;
	unsigned int lens_index;
}para_index_t;

typedef struct{
	camera_wb_flip_t wb;
	char *name;
}wb_pair_t;

typedef struct{
	camera_special_effect_t effect;
	char *name;
}effect_pair_t;

typedef struct sensor_dg_s {
    unsigned short r;
    unsigned short g;
    unsigned short b;
    unsigned short dg_default;
}sensor_dg_t;

typedef struct{
	sensor_aet_info_t *sensor_aet_info; // point to 1 of up to 16 aet information
	sensor_aet_t *sensor_aet_table;
	unsigned int sensor_aet_step; // current step of the current aet
	configure_t *configure;
}camera_priv_data_t;

int parse_config(const char *path,configure_t *cf);
int generate_para(cam_parameter_t *para,para_index_t pindex,configure_t *cf);
void free_para(cam_parameter_t *para);
int update_fmt_para(int width,int height,cam_parameter_t *para,para_index_t *pindex,configure_t *cf);

unsigned int get_aet_current_step(void *priv);
unsigned int get_aet_current_gain(void *pirv);
unsigned int get_aet_min_gain(void *priv);
unsigned int get_aet_max_gain(void *priv);
unsigned int get_aet_max_step(void *priv);
unsigned int get_aet_gain_by_step(void *priv,unsigned int new_step);


int my_i2c_put_byte(struct i2c_adapter *adapter,unsigned short i2c_addr,unsigned short addr,unsigned char data);
int my_i2c_put_byte_add8(struct i2c_adapter *adapter,unsigned short i2c_addr,char *buf,int len);
int my_i2c_get_byte(struct i2c_adapter *adapter,unsigned short i2c_addr,unsigned short addr);
int my_i2c_get_word(struct i2c_adapter *adapter,unsigned short i2c_addr);
#endif

