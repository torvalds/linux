#ifndef _REMOTE_H
#define _REMOTE_H
#include <asm/ioctl.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <plat/fiq_bridge.h>
/*remote register*/
#define LDR_ACTIVE 0x0     
#define LDR_IDLE 0x4        
#define LDR_REPEAT 0x8	 
#define DURATION_REG0    0xc             
#define OPERATION_CTRL_REG0 0x10                  
#define FRAME_BODY 0x14     
#define DURATION_REG1_AND_STATUS 0x18          
#define OPERATION_CTRL_REG1 0x1c      
#define OPERATION_CTRL_REG2 0x20 
#define DURATION_REG2    0x24    
#define DURATION_REG3    0x28    
#define FRAME_BODY1 0x2c  
#define CONFIG_END 0xff  
/*config remote register val*/
typedef struct reg_s {
	int reg;
	unsigned int val;
} remotereg_t;
typedef enum{
	NORMAL = 0,
	TIMER = 1 ,
}repeat_status;

/*
   Decode_mode.(format selection) 
   0x0 =NEC
   0x1= skip leader (just bits)
   0x2=measure width (software decode)
   0x3=MITSUBISHI
   0x4=Thomson   
   0x5=Toshiba
   0x6=Sony SIRC
   0x7=RC5
   0x8=Reserved
   0x9=RC6
   0xA=RCMM
   0xB=Duokan
   0xC=Reserved
   0xD=Reserved
   0xE=Comcast
   0xF=Sanyo
 */
typedef enum{
	DECODEMODE_NEC = 0,
	DECODEMODE_DUOKAN = 1 ,
	DECODEMODE_RCMM ,
	DECODEMODE_SONYSIRC,
	DECODEMODE_SKIPLEADER ,
	DECODEMODE_SW,
	DECODEMODE_MITSUBISHI,
	DECODEMODE_THOMSON,
	DECODEMODE_TOSHIBA,
	DECODEMODE_RC5,
	DECODEMODE_RESERVED,
	DECODEMODE_RC6,
	DECODEMODE_COMCAST,
	DECODEMODE_SANYO,
	DECODEMODE_MAX ,
	DECODEMODE_SW_NEC,
	DECODEMODE_SW_DUOKAN

}ddmode_t;

/*remote config val*/
/****************************************************************/
static const remotereg_t RDECODEMODE_NEC[] = {
	{LDR_ACTIVE,((unsigned)477<<16) | ((unsigned)400<<0)},// NEC leader 9500us,max 477: (477* timebase = 20) = 9540 ;min 400 = 8000us
	{LDR_IDLE, 248<<16 | 202<<0},// leader idle
	{LDR_REPEAT,130<<16|110<<0}, // leader repeat
	{DURATION_REG0,60<<16|48<<0 },// logic '0' or '00'      
	{OPERATION_CTRL_REG0,3<<28|(0xFA0<<12)|0x13},  // sys clock boby time.base time = 20 body frame 108ms         
	{DURATION_REG1_AND_STATUS,(111<<20)|(100<<10)}, // logic '1' or '01'      
	{OPERATION_CTRL_REG1,0x9f40},// boby long decode (8-13)
	//{OPERATION_CTRL_REG1,0xbe40},// boby long decode (8-13)
	{OPERATION_CTRL_REG2,0x0}, // hard decode mode
	{DURATION_REG2,0},
	{DURATION_REG3,0},  
	{CONFIG_END,            0      }
};
/****************************************************************/
static const remotereg_t RDECODEMODE_DUOKAN[] = {
	{LDR_ACTIVE,53<<16 | 50<<0},
	{LDR_IDLE, 31<<16 | 25<<0},
	{LDR_REPEAT,30<<16 | 26<<0},
	{DURATION_REG0,61<<16 | 55<<0 }, 
	{OPERATION_CTRL_REG0,3<<28 |(0x5DC<<12)| 0x13}, //body frame 30ms         
	{DURATION_REG1_AND_STATUS,(76<<20) | 69<<10},
	{OPERATION_CTRL_REG1,0x9300},
	{OPERATION_CTRL_REG2,0x10b},
	{DURATION_REG2,91<<16 | 79<<0},
	{DURATION_REG3,111<<16 | 99<<0},
	{CONFIG_END,            0      }
};
/****************************************************************/
static const remotereg_t RDECODEMODE_RCMM[] = {
	{LDR_ACTIVE,25<<16 | 22<<0},
	{LDR_IDLE, 14<<16 | 13<<0},
	{LDR_REPEAT,14<<16 | 13<<0},
	{DURATION_REG0,25<<16 | 21<<0 },          
	{OPERATION_CTRL_REG0,3<<28 |(0x708<<12)| 0x13}, // body frame 28 or 36 ms        
	{DURATION_REG1_AND_STATUS,33<<20 | 29<<10},        
	{OPERATION_CTRL_REG1,0xbe40},
	{OPERATION_CTRL_REG2,0xa},
	{DURATION_REG2,39<<16 | 36<<0},
	{DURATION_REG3,50<<16 | 46<<0},  
	{CONFIG_END,            0      }
};
/****************************************************************/
static const remotereg_t RDECODEMODE_SONYSIRC[] = {
	{LDR_ACTIVE,130<<16 | 110<<0},
	{LDR_IDLE, 33<<16 | 27<<0},
	{LDR_REPEAT,33<<16 | 27<<0},
	{DURATION_REG0,63<<16 | 56<<0 },          
	{OPERATION_CTRL_REG0,3<<28 |(0x8ca<<12)| 0x13},  // body frame 45ms            
	{DURATION_REG1_AND_STATUS,94<<20 | 82<<10},        
	{OPERATION_CTRL_REG1,0xbe40},
	{OPERATION_CTRL_REG2,0x6},
	{DURATION_REG2,0},
	{DURATION_REG3,0},  
	{CONFIG_END,            0      }
};

/**************************************************************/

static const remotereg_t RDECODEMODE_MITSUBISHI[] = {
	{LDR_ACTIVE,410<<16 | 390<<0},
	{LDR_IDLE, 225<<16 | 200<<0},
	{LDR_REPEAT,225<<16 | 200<<0},
	{DURATION_REG0,60<<16 | 48<<0 },          
	{OPERATION_CTRL_REG0,3<<28 |(0xBB8<<12)| 0x13},  //An IR command is repeated 60ms for as long as the key on the remote is held down. body frame 60ms            
	{DURATION_REG1_AND_STATUS,110<<20 | 95<<10},        
	{OPERATION_CTRL_REG1,0xbe40},
	{OPERATION_CTRL_REG2,0x3},
	{DURATION_REG2,0},
	{DURATION_REG3,0},
	{CONFIG_END,            0      }

};
/**********************************************************/
static const remotereg_t RDECODEMODE_TOSHIBA[] = {
	{LDR_ACTIVE,477<<16 | 389<<0},//TOSHIBA leader 9000us
	{LDR_IDLE, 477<<16 | 389<<0},// leader idle
	{LDR_REPEAT,460<<16|389<<0}, // leader repeat
	{DURATION_REG0,60<<16|40<<0 },// logic '0' or '00'      
	{OPERATION_CTRL_REG0,3<<28|(0xFA0<<12)|0x13},         
	{DURATION_REG1_AND_STATUS,111<<20|100<<10},// logic '1' or '01'      
	{OPERATION_CTRL_REG1,0xbe40},// boby long decode (8-13)
	{OPERATION_CTRL_REG2,0x5}, // hard decode mode
	{DURATION_REG2,0},
	{DURATION_REG3,0},
	{CONFIG_END,            0      }

};
/*****************************************************************/
static const remotereg_t RDECODEMODE_THOMSON[] = {
	{LDR_ACTIVE,477<<16 | 390<<0},// THOMSON leader 8000us,
	{LDR_IDLE, 477<<16 | 390<<0},// leader idle
	{LDR_REPEAT,460<<16|390<<0}, // leader repeat
	{DURATION_REG0,80<<16|60<<0 },// logic '0' or '00'      
	{OPERATION_CTRL_REG0,3<<28|(0xFA0<<12)|0x13},         
	{DURATION_REG1_AND_STATUS,140<<20|120<<10},// logic '1' or '01'      
	{OPERATION_CTRL_REG1,0xbe40},// boby long decode (8-13)
	{OPERATION_CTRL_REG2,0x4}, // hard decode mode
	{DURATION_REG2,0},
	{DURATION_REG3,0},  
	{CONFIG_END,            0      }
};
/*********************************************************************/
static const remotereg_t RDECODEMODE_COMCAST[] = {
	{LDR_ACTIVE, 0   },
	{LDR_IDLE,0  },
	{LDR_REPEAT,0	},
	{DURATION_REG0, 0},   
	{OPERATION_CTRL_REG0,0},               
	{DURATION_REG1_AND_STATUS,},        
	{OPERATION_CTRL_REG1,},
	{OPERATION_CTRL_REG2,},
	{DURATION_REG2,},
	{DURATION_REG3,},  
	{CONFIG_END,            0      }
};
static const remotereg_t RDECODEMODE_SKIPLEADER[] = {
	{LDR_ACTIVE,    },
	{LDR_IDLE,     },
	{LDR_REPEAT,	},
	{DURATION_REG0, },          
	{OPERATION_CTRL_REG0,},               
	{DURATION_REG1_AND_STATUS,},        
	{OPERATION_CTRL_REG1,},
	{OPERATION_CTRL_REG2,},
	{DURATION_REG2,},
	{DURATION_REG3,},  
	{CONFIG_END,            0      }
};
static const remotereg_t RDECODEMODE_SW[] = {
	{LDR_ACTIVE,    },
	{LDR_IDLE,     },
	{LDR_REPEAT,	},
	{DURATION_REG0, },          
	{OPERATION_CTRL_REG0,},               
	{DURATION_REG1_AND_STATUS,},        
	{OPERATION_CTRL_REG1,},
	{OPERATION_CTRL_REG2,},
	{DURATION_REG2,},
	{DURATION_REG3,},  
	{CONFIG_END,            0      }
};
static const remotereg_t RDECODEMODE_SW_NEC[] = {
	{LDR_ACTIVE,((unsigned)477<<16) | ((unsigned)400<<0)},// NEC leader 9500us,max 477: (477* timebase = 20) = 9540 ;min 400 = 8000us
	{LDR_IDLE, 248<<16 | 202<<0},// leader idle
	{LDR_REPEAT,130<<16|110<<0}, // leader repeat
	{DURATION_REG0,60<<16|48<<0 },// logic '0' or '00'
	{OPERATION_CTRL_REG0,3<<28|(0xFA0<<12)|0x13},  // sys clock boby time.base time = 20 body frame 108ms         
	{DURATION_REG1_AND_STATUS,(111<<20)|(100<<10)}, // logic '1' or '01'      
	{OPERATION_CTRL_REG1,0x8578},// boby long decode (8-13)
	{OPERATION_CTRL_REG2,0x2}, // 
	{DURATION_REG2,0},
	{DURATION_REG3,0},  
	{CONFIG_END,            0      }
};
static const remotereg_t RDECODEMODE_SW_DUOKAN[] = {
	{LDR_ACTIVE,52<<16 | 49<<0},
	{LDR_IDLE, 30<<16 | 26<<0},
	{LDR_REPEAT,30<<16 | 26<<0},
	{DURATION_REG0,60<<16 | 56<<0 },
	{OPERATION_CTRL_REG0,3<<28 |(0x5DC<<12)| 0x13}, //body frame 30ms
	{DURATION_REG1_AND_STATUS,(75<<20) | 70<<10},// logic '1' or '01'
	{OPERATION_CTRL_REG1,0x8578},// boby long decode (8-13)
	{OPERATION_CTRL_REG2,0x2}, // sw_duokan
	{DURATION_REG2,0},
	{DURATION_REG3,0},
	{CONFIG_END,            0      }
};
static const remotereg_t RDECODEMODE_RC5[] = {
	{LDR_ACTIVE,    },
	{LDR_IDLE,     },
	{LDR_REPEAT,	},
	{DURATION_REG0, },          
	{OPERATION_CTRL_REG0,},               
	{DURATION_REG1_AND_STATUS,},        
	{OPERATION_CTRL_REG1,},
	{OPERATION_CTRL_REG2,},
	{DURATION_REG2,},
	{DURATION_REG3,},  
	{CONFIG_END,            0      }
};
static const remotereg_t RDECODEMODE_RESERVED[] = {
	{LDR_ACTIVE,    },
	{LDR_IDLE,     },
	{LDR_REPEAT,	},
	{DURATION_REG0, },          
	{OPERATION_CTRL_REG0,},               
	{DURATION_REG1_AND_STATUS,},        
	{OPERATION_CTRL_REG1,},
	{OPERATION_CTRL_REG2,},
	{DURATION_REG2,},
	{DURATION_REG3,},  
	{CONFIG_END,            0      }
};
static const remotereg_t RDECODEMODE_RC6[] = {
	{LDR_ACTIVE,    },
	{LDR_IDLE,     },
	{LDR_REPEAT,	},
	{DURATION_REG0, },          
	{OPERATION_CTRL_REG0,},               
	{DURATION_REG1_AND_STATUS,},        
	{OPERATION_CTRL_REG1,},
	{OPERATION_CTRL_REG2,},
	{DURATION_REG2,},
	{DURATION_REG3,},  
	{CONFIG_END,            0      }
};



static const remotereg_t RDECODEMODE_SANYO[] = {
	{LDR_ACTIVE,    },
	{LDR_IDLE,     },
	{LDR_REPEAT,	},
	{DURATION_REG0, },          
	{OPERATION_CTRL_REG0,},               
	{DURATION_REG1_AND_STATUS,},        
	{OPERATION_CTRL_REG1,},
	{OPERATION_CTRL_REG2,},
	{DURATION_REG2,},
	{DURATION_REG3,},  
	{CONFIG_END,            0      }
};




extern unsigned int g_remote_base;
#define am_remote_write_reg(x,val) aml_write_reg32(g_remote_base +x ,val)

#define am_remote_read_reg(x) aml_read_reg32(g_remote_base +x)

#define am_remote_set_mask(x,val) aml_set_reg32_mask(g_remote_base +x,val)

#define am_remote_clear_mask(x,val) aml_clr_reg32_mask(g_remote_base +x,val)
void setremotereg(const remotereg_t *r);


//remote config  ioctl  cmd
#define REMOTE_IOC_INFCODE_CONFIG	    _IOW_BAD('I',13,sizeof(short))
#define REMOTE_IOC_RESET_KEY_MAPPING	    _IOW_BAD('I',3,sizeof(short))
#define REMOTE_IOC_SET_KEY_MAPPING		    _IOW_BAD('I',4,sizeof(short))
#define REMOTE_IOC_SET_REPEAT_KEY_MAPPING   _IOW_BAD('I',20,sizeof(short))
#define REMOTE_IOC_SET_MOUSE_MAPPING	    _IOW_BAD('I',5,sizeof(short))
#define REMOTE_IOC_SET_REPEAT_DELAY		    _IOW_BAD('I',6,sizeof(short))
#define REMOTE_IOC_SET_REPEAT_PERIOD	    _IOW_BAD('I',7,sizeof(short))

#define REMOTE_IOC_SET_REPEAT_ENABLE		_IOW_BAD('I',8,sizeof(short))
#define	REMOTE_IOC_SET_DEBUG_ENABLE			_IOW_BAD('I',9,sizeof(short))
#define	REMOTE_IOC_SET_MODE					_IOW_BAD('I',10,sizeof(short))

#define REMOTE_IOC_SET_RELEASE_DELAY		_IOW_BAD('I',99,sizeof(short))
#define REMOTE_IOC_SET_CUSTOMCODE   		_IOW_BAD('I',100,sizeof(short))
//reg
#define REMOTE_IOC_SET_REG_BASE_GEN			_IOW_BAD('I',101,sizeof(short))
#define REMOTE_IOC_SET_REG_CONTROL			_IOW_BAD('I',102,sizeof(short))
#define REMOTE_IOC_SET_REG_LEADER_ACT 		_IOW_BAD('I',103,sizeof(short))
#define REMOTE_IOC_SET_REG_LEADER_IDLE 		_IOW_BAD('I',104,sizeof(short))
#define REMOTE_IOC_SET_REG_REPEAT_LEADER 	_IOW_BAD('I',105,sizeof(short))
#define REMOTE_IOC_SET_REG_BIT0_TIME		 _IOW_BAD('I',106,sizeof(short))

//sw
#define REMOTE_IOC_SET_BIT_COUNT		 	_IOW_BAD('I',107,sizeof(short))
#define REMOTE_IOC_SET_TW_LEADER_ACT		_IOW_BAD('I',108,sizeof(short))
#define REMOTE_IOC_SET_TW_BIT0_TIME			_IOW_BAD('I',109,sizeof(short))
#define REMOTE_IOC_SET_TW_BIT1_TIME			_IOW_BAD('I',110,sizeof(short))
#define REMOTE_IOC_SET_TW_REPEATE_LEADER	_IOW_BAD('I',111,sizeof(short))

#define REMOTE_IOC_GET_TW_LEADER_ACT		_IOR_BAD('I',112,sizeof(short))
#define REMOTE_IOC_GET_TW_BIT0_TIME			_IOR_BAD('I',113,sizeof(short))
#define REMOTE_IOC_GET_TW_BIT1_TIME			_IOR_BAD('I',114,sizeof(short))
#define REMOTE_IOC_GET_TW_REPEATE_LEADER	_IOR_BAD('I',115,sizeof(short))

#define REMOTE_IOC_GET_REG_BASE_GEN			_IOR_BAD('I',121,sizeof(short))
#define REMOTE_IOC_GET_REG_CONTROL			_IOR_BAD('I',122,sizeof(short))
#define REMOTE_IOC_GET_REG_LEADER_ACT 		_IOR_BAD('I',123,sizeof(short))
#define REMOTE_IOC_GET_REG_LEADER_IDLE 		_IOR_BAD('I',124,sizeof(short))
#define REMOTE_IOC_GET_REG_REPEAT_LEADER 	_IOR_BAD('I',125,sizeof(short))
#define REMOTE_IOC_GET_REG_BIT0_TIME	 	_IOR_BAD('I',126,sizeof(short))
#define REMOTE_IOC_GET_REG_FRAME_DATA		_IOR_BAD('I',127,sizeof(short))
#define REMOTE_IOC_GET_REG_FRAME_STATUS		_IOR_BAD('I',128,sizeof(short))

#define REMOTE_IOC_SET_TW_BIT2_TIME			_IOW_BAD('I',129,sizeof(short))
#define REMOTE_IOC_SET_TW_BIT3_TIME			_IOW_BAD('I',130,sizeof(short))

#define   REMOTE_IOC_SET_FN_KEY_SCANCODE     _IOW_BAD('I', 131, sizeof(short))
#define   REMOTE_IOC_SET_LEFT_KEY_SCANCODE   _IOW_BAD('I', 132, sizeof(short))
#define   REMOTE_IOC_SET_RIGHT_KEY_SCANCODE  _IOW_BAD('I', 133, sizeof(short))
#define   REMOTE_IOC_SET_UP_KEY_SCANCODE     _IOW_BAD('I', 134, sizeof(short))
#define   REMOTE_IOC_SET_DOWN_KEY_SCANCODE   _IOW_BAD('I', 135, sizeof(short))
#define   REMOTE_IOC_SET_OK_KEY_SCANCODE     _IOW_BAD('I', 136, sizeof(short))
#define   REMOTE_IOC_SET_PAGEUP_KEY_SCANCODE _IOW_BAD('I', 137, sizeof(short))
#define   REMOTE_IOC_SET_PAGEDOWN_KEY_SCANCODE _IOW_BAD('I', 138, sizeof(short))
#define   REMOTE_IOC_SET_RELT_DELAY	    _IOW_BAD('I',140,sizeof(short))

#define	REMOTE_HW_DECODER_STATUS_MASK		(0xf<<4)
#define	REMOTE_HW_DECODER_STATUS_OK			(0<<4)
#define	REMOTE_HW_DECODER_STATUS_TIMEOUT	(1<<4)
#define	REMOTE_HW_DECODER_STATUS_LEADERERR	(2<<4)
#define	REMOTE_HW_DECODER_STATUS_REPEATERR	(3<<4)

/* software  decode status*/
#define REMOTE_STATUS_WAIT       0
#define REMOTE_STATUS_LEADER     1
#define REMOTE_STATUS_DATA       2
#define REMOTE_STATUS_SYNC       3

#define REPEARTFLAG 0x1 //status register repeat set flag
#define KEYDOMIAN 1 // find key val vail data domain
#define CUSTOMDOMAIN 0 // find key val vail custom domain
/*phy page user debug*/
#define REMOTE_LOG_BUF_LEN		 8192
#define REMOTE_LOG_BUF_ORDER		1


typedef int (*type_printk)(const char *fmt, ...);
/* this is a message of IR input device,include release timer repeat timer*/
/*
 */
struct remote {
	struct input_dev *input;
	struct timer_list timer;  //release timer
	struct timer_list repeat_timer;  //repeat timer
	struct timer_list rel_timer;  //repeat timer
	unsigned long repeat_tick;
	int irq;
	int save_mode;
	int work_mode; // use ioctl config decode mode
	int frame_mode;// same protocol frame have diffrent mode
	unsigned int register_data;
	unsigned int frame_status;
	unsigned int cur_keycode;
	unsigned int cur_lsbkeycode; // rcv low 32bit save
	unsigned int cur_msbkeycode; // rcv high 10bit save
	unsigned int repeat_release_code;// save 
	unsigned int last_keycode;
	unsigned int repeate_flag;
	unsigned int repeat_enable;
	unsigned int debounce;
	unsigned int status;
	// we can only support 20 maptable
	int map_num;
	int ig_custom_enable;
	int enable_repeat_falg;
	unsigned int custom_code[20];
	//use duble protocol release time
	unsigned int release_fdelay; //frist protocol
	unsigned int release_sdelay;// second protocol
	unsigned int release_delay;
	// debug swtich
	unsigned int debug_enable;
	//sw
	unsigned int sleep;
	unsigned int delay;
	unsigned int step;//sw status
	unsigned int send_data;
	bridge_item_t fiq_handle_item;
	int want_repeat_enable;
	unsigned int key_repeat_map[20][256];
	unsigned int bit_count;
	unsigned int bit_num;
	unsigned int last_jiffies;
	unsigned int time_window[18];//
	int last_pulse_width;
	int repeat_time_count;
	//config
	int config_major;
	char config_name[20];
	struct class *config_class;
	struct device *config_dev;
	unsigned int repeat_delay;
	unsigned int relt_delay;
	unsigned int repeat_peroid;
	//
	int (*remote_reprot_press_key)(struct remote *);
	int (*key_report)(struct remote *);
	void (*key_release_report)(struct remote *);
	void (*remote_send_key)(struct input_dev *, unsigned int,unsigned int,int);
};

extern type_printk input_dbg;

int set_remote_mode(int mode);
void set_remote_init(struct remote *remote_data);
void kdb_send_key(struct input_dev *dev, unsigned int scancode,unsigned int type,int event);
void remote_send_key(struct input_dev *dev, unsigned int scancode,
		unsigned int type,int event);
extern irqreturn_t remote_bridge_isr(int irq, void *dev_id);
extern int remote_hw_reprot_key(struct remote *remote_data);
extern int remote_sw_reprot_key(struct remote *remote_data);
extern void remote_nec_report_release_key(struct remote *remote_data);
extern void remote_duokan_report_release_key(struct remote *remote_data);
extern void remote_sw_reprot_release_key(struct remote *remote_data);




extern int register_fiq_bridge_handle(bridge_item_t * c_item);
extern int unregister_fiq_bridge_handle(bridge_item_t * c_item);
extern int fiq_bridge_pulse_trigger(bridge_item_t * c_item);



#endif //_REMOTE_H
