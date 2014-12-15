/*
 * amvecm char device driver.
 *
 * Copyright (c) 2010 Frank Zhao<frank.zhao@amlogic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/* Standard Linux headers */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <asm/uaccess.h>

#include <linux/amlogic/aml_common.h>


#include <mach/am_regs.h>
#include <linux/amlogic/vframe.h>

#include <linux/amlogic/amvecm.h>        /* For user used */
#include "ve_regs.h"
#include "amve.h"
#include "cm_regs.h"
#include "amcm.h"

/* local defines */
#define AMVECM_COUNT              1

#define AMVECM_NAME               "amvecm"
#define AMVECM_DRIVER_NAME        "amvecm"
#define AMVECM_MODULE_NAME        "amvecm"
#define AMVECM_DEVICE_NAME        "amvecm"
#define AMVECM_CLASS_NAME         "amvecm"
typedef struct amvecm_dev_s {
    dev_t                       devt;
    struct cdev                 cdev;
    dev_t                       devno;
    struct device               *dev;
    struct class                *clsp;
} amvecm_dev_t;

static bool hold_cmd_en = 0;
module_param(hold_cmd_en, bool, 0664);
MODULE_PARM_DESC(hold_cmd_en, "\n hold_cmd_en \n");

static int cm_level = 0;//0:optimize;1:enhancement
module_param(cm_level, int, 0664);
MODULE_PARM_DESC(cm_level, "\n selcet cm lever \n");

static int cm_en = 0;//0:disabel;1:enable
module_param(cm_en, int, 0664);
MODULE_PARM_DESC(cm_en, "\n enable or disable cm \n");
static int cm_level_last = 0;//0:optimize;1:enhancement

static int dnlp_en = 0;//0:disabel;1:enable
module_param(dnlp_en, int, 0664);
MODULE_PARM_DESC(dnlp_en, "\n enable or disable dnlp \n");
static int dnlp_status = 1;//0:done;1:todo

static struct amvecm_dev_s amvecm_dev;
static struct ve_dnlp_s am_ve_dnlp;
static struct ve_dnlp_table_s am_ve_new_dnlp;

unsigned int vecm_latch_flag;
module_param(vecm_latch_flag, uint, 0664);
MODULE_PARM_DESC(vecm_latch_flag, "\n vecm_latch_flag \n");

unsigned int cm_size,ve_size;
static int video_rgb_ogo_mode_sw = 0;
static signed int vd1_brightness = 0, vd1_contrast = 0;
extern unsigned int cm2_patch_flag;

static struct am_regs_s amregs0;
static struct am_regs_s amregs1;
static struct am_regs_s amregs2;
static struct am_regs_s amregs3;
static struct am_regs_s amregs4;
static struct am_regs_s amregs5;
struct am_regs_s cmreg_lever1={
	172,
	{
/* optimize */
{6,	512,	    0xffffffff,	0           },
{6,	513,	    0xffffffff,	0           },
{6,	514,	    0xffffffff,	0           },
{6,	515,	    0xffffffff,	134218752   },
{6,	516,	    0xffffffff,	144         },
{6,	518,	    0xffffffff,	0           },
{6,	519,	    0xffffffff,	33554432    },
{6,	520,	    0xffffffff,	118         },
{6,	521,	    0xffffffff,	0           },
{6,	522,	    0xffffffff,	0           },
{6,	527,	    0xf,	0           },
{6,	517,	    0xffffffff,	70715263    },
{9,	256,	    0xffffffff,	532992      },
{9,	257,	    0xffffffff,	0xf3f600af  },
{9,	258,	    0xffffffff,	0xe3000000  },
{9,	259,	    0xffffffff,	240         },
{9,	260,	    0xffffffff,	63506       },
{9,	264,	    0xffffffff,	16650496    },
{9,	265,	    0xffffffff,	249         },
{9,	266,	    0xffffffff,	0           },
{9,	267,	    0xffffffff,	0           },
{9,	268,	    0xffffffff,	0           },
{9,	272,	    0xffffffff,	135936      },
{9,	273,	    0xffffffff,	253         },
{9,	274,	    0xffffffff,	0           },
{9,	275,	    0xffffffff,	0           },
{9,	276,	    0xffffffff,	0           },
{9,	280,	    0xffffffff,	0xf3f11300  },
{9,	281,	    0xffffffff,	214         },
{9,	282,	    0xffffffff,	0           },
{9,	283,	    0xffffffff,	0           },
{9,	284,	    0xffffffff,	0           },
{9,	288,	    0xffffffff,	0xede91300  },
{9,	289,	    0xffffffff,	177         },
{9,	290,	    0xffffffff,	0           },
{9,	291,	    0xffffffff,	0           },
{9,	292,	    0xffffffff,	15660544    },
{9,	296,	    0xffffffff,	0xe9e61100  },
{9,	297,	    0xffffffff,	184         },
{9,	298,	    0xffffffff,	0           },
{9,	299,	    0xffffffff,	0           },
{9,	300,	    0xffffffff,	15464448    },
{9,	304,	    0xffffffff,	0xeff80f00  },
{9,	305,	    0xffffffff,	232         },
{9,	306,	    0xffffffff,	0           },
{9,	307,	    0xffffffff,	0           },
{9,	308,	    0xffffffff,	0           },
{9,	312,	    0xffffffff,	0xf60a0d00  },
{9,	313,	    0xffffffff,	245         },
{9,	314,	    0xffffffff,	0           },
{9,	315,	    0xffffffff,	0           },
{9,	316,	    0xffffffff,	0           },
{9,	320,	    0xffffffff,	526848      },
{9,	321,	    0xffffffff,	238         },
{9,	322,	    0xffffffff,	0           },
{9,	323,	    0xffffffff,	10          },
{9,	324,	    0xffffffff,	0           },
{9,	328,	    0xffffffff,	527616      },
{9,	329,	    0xffffffff,	134742245   },
{9,	330,	    0xffffffff,	167903232   },
{9,	331,	    0xffffffff,	21          },
{9,	332,	    0xffffffff,	3328        },
{9,	336,	    0xffffffff,	397056      },
{9,	337,	    0xffffffff,	0xf80d00da  },
{9,	338,	    0xffffffff,	0xf80500fb  },
{9,	339,	    0xffffffff,	2           },
{9,	340,	    0xffffffff,	132357      },
{9,	344,	    0xffffffff,	526848      },
{9,	345,	    0xffffffff,	0xfb0a00c7  },
{9,	346,	    0xffffffff,	0xf6020002  },
{9,	347,	    0xffffffff,	251         },
{9,	348,	    0xffffffff,	16646656    },
{9,	352,	    0xffffffff,	657408      },
{9,	353,	    0xffffffff,	327857      },
{9,	354,	    0xffffffff,	0xf6050000  },
{9,	355,	    0xffffffff,	2           },
{9,	356,	    0xffffffff,	0           },
{9,	360,	    0xffffffff,	526336      },
{9,	361,	    0xffffffff,	162         },
{9,	362,	    0xffffffff,	0xf600f600  },
{9,	363,	    0xffffffff,	254         },
{9,	364,	    0xffffffff,	33554432    },
{9,	368,	    0xffffffff,	986368      },
{9,	369,	    0xffffffff,	166         },
{9,	370,	    0xffffffff,	0xf300eef6  },
{9,	371,	    0xffffffff,	243         },
{9,	372,	    0xffffffff,	0xfb050000  },
{9,	376,	    0xffffffff,	34541312    },
{9,	377,	    0xffffffff,	83886277    },
{9,	378,	    0xffffffff,	0xfbfef8f8  },
{9,	379,	    0xffffffff,	246         },
{9,	380,	    0xffffffff,	0xf8080200  },
{9,	384,	    0xffffffff,	286331136   },
{9,	385,	    0xffffffff,	353698041   },
{9,	386,	    0xffffffff,	168427768   },
{9,	387,	    0xffffffff,	13          },
{9,	388,	    0xffffffff,	0xfb081000  },
{9,	392,	    0xffffffff,	218960128   },
{9,	393,	    0xffffffff,	674758687   },
{9,	394,	    0xffffffff,	573177856   },
{9,	395,	    0xffffffff,	21          },
{9,	396,	    0xffffffff,	854766      },
{9,	400,	    0xffffffff,	168430080   },
{9,	401,	    0xffffffff,	809304110   },
{9,	402,	    0xffffffff,	674234389   },
{9,	403,	    0xffffffff,	13          },
{9,	404,	    0xffffffff,	716515      },
{9,	408,	    0xffffffff,	134745344   },
{9,	409,	    0xffffffff,	807731246   },
{9,	410,	    0xffffffff,	403308562   },
{9,	411,	    0xffffffff,	0           },
{9,	412,	    0xffffffff,	59590       },
{9,	416,	    0xffffffff,	67504640    },
{9,	417,	    0xffffffff,	269484072   },
{9,	418,	    0xffffffff,	182845440   },
{9,	419,	    0xffffffff,	0           },
{9,	420,	    0xffffffff,	61139       },
{9,	424,	    0xffffffff,	396544      },
{9,	425,	    0xffffffff,	134217757   },
{9,	426,	    0xffffffff,	0xfb0b0000  },
{9,	427,	    0xffffffff,	0           },
{9,	428,	    0xffffffff,	246         },
{9,	432,	    0xffffffff,	527616      },
{9,	433,	    0xffffffff,	12          },
{9,	434,	    0xffffffff,	15597568    },
{9,	435,	    0xffffffff,	0           },
{9,	436,	    0xffffffff,	0           },
{9,	440,	    0xffffffff,	658688      },
{9,	441,	    0xffffffff,	7           },
{9,	442,	    0xffffffff,	0           },
{9,	443,	    0xffffffff,	0           },
{9,	444,	    0xffffffff,	0           },
{9,	448,	    0xffffffff,	986880      },
{9,	449,	    0xffffffff,	255         },
{9,	450,	    0xffffffff,	0           },
{9,	451,	    0xffffffff,	0           },
{9,	452,	    0xffffffff,	0           },
{9,	456,	    0xffffffff,	219811584   },
{9,	457,	    0xffffffff,	238         },
{9,	458,	    0xffffffff,	0           },
{9,	459,	    0xffffffff,	0           },
{9,	460,	    0xffffffff,	0           },
{9,	464,	    0xffffffff,	287183104   },
{9,	465,	    0xffffffff,	197         },
{9,	466,	    0xffffffff,	0xf3000000  },
{9,	467,	    0xffffffff,	246         },
{9,	468,	    0xffffffff,	0xfb000000  },
{9,	472,	    0xffffffff,	219943168   },
{9,	473,	    0xffffffff,	169         },
{9,	474,	    0xffffffff,	0xf0000000  },
{9,	475,	    0xffffffff,	232         },
{9,	476,	    0xffffffff,	0           },
{9,	480,	    0xffffffff,	1707264     },
{9,	481,	    0xffffffff,	0xfb0000ab  },
{9,	482,	    0xffffffff,	0xe300f3f3  },
{9,	483,	    0xffffffff,	219         },
{9,	484,	    0xffffffff,	15597568    },
{9,	488,	    0xffffffff,	1509888     },
{9,	489,	    0xffffffff,	0xf30000b6  },
{9,	490,	    0xffffffff,	0xd800e8e3  },
{9,	491,	    0xffffffff,	222         },
{9,	492,	    0xffffffff,	14876680    },
{9,	496,	    0xffffffff,	1117440     },
{9,	497,	    0xffffffff,	0xf3fb00ba  },
{9,	498,	    0xffffffff,	0xd300ebe6  },
{9,	499,	    0xffffffff,	232         },
{9,	500,	    0xffffffff,	15661840    },
{9,	504,	    0xffffffff,	989696      },
{9,	505,	    0xffffffff,	0xf6f800b6  },
{9,	506,	    0xffffffff,	0xe600f3f3  },
{9,	507,	    0xffffffff,	235         },
{9,	508,	    0xffffffff,	18          },
{0}
	}
};
struct am_regs_s cmreg_lever2={
	172,
	{
/* optimize */
{6,	512,	    0xffffffff,	0          },
{6,	513,	    0xffffffff,	0          },
{6,	514,	    0xffffffff,	0          },
{6,	515,	    0xffffffff,	134218752  },
{6,	516,	    0xffffffff,	144        },
{6,	518,	    0xffffffff,	0          },
{6,	519,	    0xffffffff,	33554432   },
{6,	520,	    0xffffffff,	122        },
{6,	521,	    0xffffffff,	0          },
{6,	522,	    0xffffffff,	0          },
{6,	527,	    0xf,	0          },
{6,	517,	    0xffffffff,	70715263   },
{9,	256,	    0xffffffff,	2840064    },
{9,	257,	    0xffffffff,	12         },
{9,	258,	    0xffffffff,	0          },
{9,	259,	    0xffffffff,	0          },
{9,	260,	    0xffffffff,	0          },
{9,	264,	    0xffffffff,	2708992    },
{9,	265,	    0xffffffff,	31         },
{9,	266,	    0xffffffff,	0          },
{9,	267,	    0xffffffff,	0          },
{9,	268,	    0xffffffff,	0          },
{9,	272,	    0xffffffff,	2708992    },
{9,	273,	    0xffffffff,	51         },
{9,	274,	    0xffffffff,	0          },
{9,	275,	    0xffffffff,	0          },
{9,	276,	    0xffffffff,	0          },
{9,	280,	    0xffffffff,	2840064    },
{9,	281,	    0xffffffff,	63         },
{9,	282,	    0xffffffff,	0          },
{9,	283,	    0xffffffff,	0          },
{9,	284,	    0xffffffff,	0          },
{9,	288,	    0xffffffff,	2840064    },
{9,	289,	    0xffffffff,	63         },
{9,	290,	    0xffffffff,	0          },
{9,	291,	    0xffffffff,	0          },
{9,	292,	    0xffffffff,	0          },
{9,	296,	    0xffffffff,	2708992    },
{9,	297,	    0xffffffff,	63         },
{9,	298,	    0xffffffff,	0          },
{9,	299,	    0xffffffff,	0          },
{9,	300,	    0xffffffff,	0          },
{9,	304,	    0xffffffff,	2708480    },
{9,	305,	    0xffffffff,	63         },
{9,	306,	    0xffffffff,	0          },
{9,	307,	    0xffffffff,	0          },
{9,	308,	    0xffffffff,	0          },
{9,	312,	    0xffffffff,	2708480    },
{9,	313,	    0xffffffff,	51         },
{9,	314,	    0xffffffff,	0          },
{9,	315,	    0xffffffff,	0          },
{9,	316,	    0xffffffff,	0          },
{9,	320,	    0xffffffff,	2708480    },
{9,	321,	    0xffffffff,	9          },
{9,	322,	    0xffffffff,	0          },
{9,	323,	    0xffffffff,	0          },
{9,	324,	    0xffffffff,	0          },
{9,	328,	    0xffffffff,	2708480    },
{9,	329,	    0xffffffff,	234        },
{9,	330,	    0xffffffff,	0          },
{9,	331,	    0xffffffff,	0          },
{9,	332,	    0xffffffff,	0          },
{9,	336,	    0xffffffff,	2708480    },
{9,	337,	    0xffffffff,	212        },
{9,	338,	    0xffffffff,	0          },
{9,	339,	    0xffffffff,	0          },
{9,	340,	    0xffffffff,	0          },
{9,	344,	    0xffffffff,	2708480    },
{9,	345,	    0xffffffff,	197        },
{9,	346,	    0xffffffff,	0          },
{9,	347,	    0xffffffff,	0          },
{9,	348,	    0xffffffff,	0          },
{9,	352,	    0xffffffff,	2708480    },
{9,	353,	    0xffffffff,	193        },
{9,	354,	    0xffffffff,	0          },
{9,	355,	    0xffffffff,	0          },
{9,	356,	    0xffffffff,	0          },
{9,	360,	    0xffffffff,	2708480    },
{9,	361,	    0xffffffff,	203        },
{9,	362,	    0xffffffff,	0          },
{9,	363,	    0xffffffff,	0          },
{9,	364,	    0xffffffff,	0          },
{9,	368,	    0xffffffff,	2708480    },
{9,	369,	    0xffffffff,	214        },
{9,	370,	    0xffffffff,	0          },
{9,	371,	    0xffffffff,	0          },
{9,	372,	    0xffffffff,	0          },
{9,	376,	    0xffffffff,	2708992    },
{9,	377,	    0xffffffff,	219        },
{9,	378,	    0xffffffff,	0          },
{9,	379,	    0xffffffff,	0          },
{9,	380,	    0xffffffff,	0          },
{9,	384,	    0xffffffff,	2708480    },
{9,	385,	    0xffffffff,	236        },
{9,	386,	    0xffffffff,	0          },
{9,	387,	    0xffffffff,	0          },
{9,	388,	    0xffffffff,	0          },
{9,	392,	    0xffffffff,	2708992    },
{9,	393,	    0xffffffff,	249        },
{9,	394,	    0xffffffff,	0          },
{9,	395,	    0xffffffff,	0          },
{9,	396,	    0xffffffff,	0          },
{9,	400,	    0xffffffff,	2708992    },
{9,	401,	    0xffffffff,	3          },
{9,	402,	    0xffffffff,	0          },
{9,	403,	    0xffffffff,	0          },
{9,	404,	    0xffffffff,	0          },
{9,	408,	    0xffffffff,	2708992    },
{9,	409,	    0xffffffff,	9          },
{9,	410,	    0xffffffff,	0          },
{9,	411,	    0xffffffff,	0          },
{9,	412,	    0xffffffff,	0          },
{9,	416,	    0xffffffff,	2708480    },
{9,	417,	    0xffffffff,	12         },
{9,	418,	    0xffffffff,	0          },
{9,	419,	    0xffffffff,	0          },
{9,	420,	    0xffffffff,	0          },
{9,	424,	    0xffffffff,	2708480    },
{9,	425,	    0xffffffff,	14         },
{9,	426,	    0xffffffff,	0          },
{9,	427,	    0xffffffff,	0          },
{9,	428,	    0xffffffff,	0          },
{9,	432,	    0xffffffff,	2708480    },
{9,	433,	    0xffffffff,	12         },
{9,	434,	    0xffffffff,	0          },
{9,	435,	    0xffffffff,	0          },
{9,	436,	    0xffffffff,	0          },
{9,	440,	    0xffffffff,	2708480    },
{9,	441,	    0xffffffff,	7          },
{9,	442,	    0xffffffff,	0          },
{9,	443,	    0xffffffff,	0          },
{9,	444,	    0xffffffff,	0          },
{9,	448,	    0xffffffff,	2708480    },
{9,	449,	    0xffffffff,	251        },
{9,	450,	    0xffffffff,	0          },
{9,	451,	    0xffffffff,	0          },
{9,	452,	    0xffffffff,	0          },
{9,	456,	    0xffffffff,	2708480    },
{9,	457,	    0xffffffff,	242        },
{9,	458,	    0xffffffff,	0          },
{9,	459,	    0xffffffff,	0          },
{9,	460,	    0xffffffff,	0          },
{9,	464,	    0xffffffff,	36262912   },
{9,	465,	    0xffffffff,	238        },
{9,	466,	    0xffffffff,	0          },
{9,	467,	    0xffffffff,	0          },
{9,	468,	    0xffffffff,	0          },
{9,	472,	    0xffffffff,	69817344   },
{9,	473,	    0xffffffff,	245        },
{9,	474,	    0xffffffff,	0          },
{9,	475,	    0xffffffff,	0          },
{9,	476,	    0xffffffff,	0          },
{9,	480,	    0xffffffff,	136926208  },
{9,	481,	    0xffffffff,	1          },
{9,	482,	    0xffffffff,	0          },
{9,	483,	    0xffffffff,	0          },
{9,	484,	    0xffffffff,	0          },
{9,	488,	    0xffffffff,	136926208  },
{9,	489,	    0xffffffff,	5          },
{9,	490,	    0xffffffff,	0          },
{9,	491,	    0xffffffff,	0          },
{9,	492,	    0xffffffff,	0          },
{9,	496,	    0xffffffff,	136926208  },
{9,	497,	    0xffffffff,	5          },
{9,	498,	    0xffffffff,	0          },
{9,	499,	    0xffffffff,	0          },
{9,	500,	    0xffffffff,	0          },
{9,	504,	    0xffffffff,	69817344   },
{9,	505,	    0xffffffff,	7          },
{9,	506,	    0xffffffff,	0          },
{9,	507,	    0xffffffff,	0          },
{9,	508,	    0xffffffff,	0          },
{0}
	}
};
struct am_regs_s cmreg_lever3={
	172,
	{
/* optimize */
{6,	512,	    0xffffffff,	0          },
{6,	513,	    0xffffffff,	0          },
{6,	514,	    0xffffffff,	0          },
{6,	515,	    0xffffffff,	134218752  },
{6,	516,	    0xffffffff,	144        },
{6,	518,	    0xffffffff,	0          },
{6,	519,	    0xffffffff,	33554432   },
{6,	520,	    0xffffffff,	118        },
{6,	521,	    0xffffffff,	62914560   },
{6,	522,	    0xffffffff,	70778880   },
{6,	527,	           0xf,	1          },
{6,	517,	    0xffffffff,	70780799   },
{9,	256,	    0xffffffff,	16648704   },
{9,	257,	    0xffffffff,	0xf3f6000b },
{9,	258,	    0xffffffff,	0xe3000000 },
{9,	259,	    0xffffffff,	240        },
{9,	260,	    0xffffffff,	63742      },
{9,	264,	    0xffffffff,	16650496   },
{9,	265,	    0xffffffff,	14         },
{9,	266,	    0xffffffff,	0          },
{9,	267,	    0xffffffff,	0          },
{9,	268,	    0xffffffff,	0          },
{9,	272,	    0xffffffff,	135936     },
{9,	273,	    0xffffffff,	253        },
{9,	274,	    0xffffffff,	0          },
{9,	275,	    0xffffffff,	0          },
{9,	276,	    0xffffffff,	0          },
{9,	280,	    0xffffffff,	0xf3f11300 },
{9,	281,	    0xffffffff,	214        },
{9,	282,	    0xffffffff,	0          },
{9,	283,	    0xffffffff,	0          },
{9,	284,	    0xffffffff,	0          },
{9,	288,	    0xffffffff,	0xede91300 },
{9,	289,	    0xffffffff,	177        },
{9,	290,	    0xffffffff,	0          },
{9,	291,	    0xffffffff,	0          },
{9,	292,	    0xffffffff,	15660544   },
{9,	296,	    0xffffffff,	0xe9e61100 },
{9,	297,	    0xffffffff,	184        },
{9,	298,	    0xffffffff,	0          },
{9,	299,	    0xffffffff,	0          },
{9,	300,	    0xffffffff,	15464448   },
{9,	304,	    0xffffffff,	0xeff80f00 },
{9,	305,	    0xffffffff,	232        },
{9,	306,	    0xffffffff,	0          },
{9,	307,	    0xffffffff,	0          },
{9,	308,	    0xffffffff,	0          },
{9,	312,	    0xffffffff,	0xf60a0d00 },
{9,	313,	    0xffffffff,	5          },
{9,	314,	    0xffffffff,	0          },
{9,	315,	    0xffffffff,	0          },
{9,	316,	    0xffffffff,	0          },
{9,	320,	    0xffffffff,	526848     },
{9,	321,	    0xffffffff,	5          },
{9,	322,	    0xffffffff,	0          },
{9,	323,	    0xffffffff,	10         },
{9,	324,	    0xffffffff,	0          },
{9,	328,	    0xffffffff,	526848     },
{9,	329,	    0xffffffff,	134742265  },
{9,	330,	    0xffffffff,	167903232  },
{9,	331,	    0xffffffff,	21         },
{9,	332,	    0xffffffff,	3328       },
{9,	336,	    0xffffffff,	131072     },
{9,	337,	    0xffffffff,	0xf80d00ea },
{9,	338,	    0xffffffff,	0xf80500fb },
{9,	339,	    0xffffffff,	2          },
{9,	340,	    0xffffffff,	132357     },
{9,	344,	    0xffffffff,	131584     },
{9,	345,	    0xffffffff,	0xfb0100da },
{9,	346,	    0xffffffff,	0xf6020002 },
{9,	347,	    0xffffffff,	251        },
{9,	348,	    0xffffffff,	16646656   },
{9,	352,	    0xffffffff,	657408     },
{9,	353,	    0xffffffff,	327872     },
{9,	354,	    0xffffffff,	0xf6050000 },
{9,	355,	    0xffffffff,	2          },
{9,	356,	    0xffffffff,	0          },
{9,	360,	    0xffffffff,	526336     },
{9,	361,	    0xffffffff,	175        },
{9,	362,	    0xffffffff,	0xf600f600 },
{9,	363,	    0xffffffff,	254        },
{9,	364,	    0xffffffff,	33554432   },
{9,	368,	    0xffffffff,	986368     },
{9,	369,	    0xffffffff,	169        },
{9,	370,	    0xffffffff,	0xf300eef6 },
{9,	371,	    0xffffffff,	243        },
{9,	372,	    0xffffffff,	0xfb050000 },
{9,	376,	    0xffffffff,	34541312   },
{9,	377,	    0xffffffff,	83886277   },
{9,	378,	    0xffffffff,	0xfbfef8f8 },
{9,	379,	    0xffffffff,	246        },
{9,	380,	    0xffffffff,	0xf8080200 },
{9,	384,	    0xffffffff,	135339520  },
{9,	385,	    0xffffffff,	353697836  },
{9,	386,	    0xffffffff,	168427768  },
{9,	387,	    0xffffffff,	13         },
{9,	388,	    0xffffffff,	0xfb080800 },
{9,	392,	    0xffffffff,	219356160  },
{9,	393,	    0xffffffff,	674758739  },
{9,	394,	    0xffffffff,	573177856  },
{9,	395,	    0xffffffff,	21         },
{9,	396,	    0xffffffff,	657920     },
{9,	400,	    0xffffffff,	219223808  },
{9,	401,	    0xffffffff,	809304160  },
{9,	402,	    0xffffffff,	674562069  },
{9,	403,	    0xffffffff,	13         },
{9,	404,	    0xffffffff,	657920     },
{9,	408,	    0xffffffff,	168629504  },
{9,	409,	    0xffffffff,	807731289  },
{9,	410,	    0xffffffff,	403308562  },
{9,	411,	    0xffffffff,	0          },
{9,	412,	    0xffffffff,	526848     },
{9,	416,	    0xffffffff,	395776     },
{9,	417,	    0xffffffff,	269484098  },
{9,	418,	    0xffffffff,	167772160  },
{9,	419,	    0xffffffff,	0          },
{9,	420,	    0xffffffff,	0          },
{9,	424,	    0xffffffff,	396544     },
{9,	425,	    0xffffffff,	134217757  },
{9,	426,	    0xffffffff,	0          },
{9,	427,	    0xffffffff,	0          },
{9,	428,	    0xffffffff,	0          },
{9,	432,	    0xffffffff,	527616     },
{9,	433,	    0xffffffff,	12         },
{9,	434,	    0xffffffff,	0          },
{9,	435,	    0xffffffff,	0          },
{9,	436,	    0xffffffff,	0          },
{9,	440,	    0xffffffff,	658688     },
{9,	441,	    0xffffffff,	7          },
{9,	442,	    0xffffffff,	0          },
{9,	443,	    0xffffffff,	0          },
{9,	444,	    0xffffffff,	0          },
{9,	448,	    0xffffffff,	986880     },
{9,	449,	    0xffffffff,	5          },
{9,	450,	    0xffffffff,	0          },
{9,	451,	    0xffffffff,	0          },
{9,	452,	    0xffffffff,	0          },
{9,	456,	    0xffffffff,	219811584  },
{9,	457,	    0xffffffff,	244        },
{9,	458,	    0xffffffff,	0          },
{9,	459,	    0xffffffff,	0          },
{9,	460,	    0xffffffff,	0          },
{9,	464,	    0xffffffff,	287183104  },
{9,	465,	    0xffffffff,	186        },
{9,	466,	    0xffffffff,	0xf3000000 },
{9,	467,	    0xffffffff,	246        },
{9,	468,	    0xffffffff,	0xfb000000 },
{9,	472,	    0xffffffff,	219944192  },
{9,	473,	    0xffffffff,	158        },
{9,	474,	    0xffffffff,	0xf0000000 },
{9,	475,	    0xffffffff,	232        },
{9,	476,	    0xffffffff,	0          },
{9,	480,	    0xffffffff,	1382912    },
{9,	481,	    0xffffffff,	0xfb0000a4 },
{9,	482,	    0xffffffff,	0xe300f3f3 },
{9,	483,	    0xffffffff,	219        },
{9,	484,	    0xffffffff,	0          },
{9,	488,	    0xffffffff,	1514496    },
{9,	489,	    0xffffffff,	0xf30000ba },
{9,	490,	    0xffffffff,	0xe600f3f0 },
{9,	491,	    0xffffffff,	222        },
{9,	492,	    0xffffffff,	0          },
{9,	496,	    0xffffffff,	1120000    },
{9,	497,	    0xffffffff,	0xf3fb00db },
{9,	498,	    0xffffffff,	0xe800f6ee },
{9,	499,	    0xffffffff,	232        },
{9,	500,	    0xffffffff,	64256      },
{9,	504,	    0xffffffff,	397056     },
{9,	505,	    0xffffffff,	0xf6f800f7 },
{9,	506,	    0xffffffff,	0xe60000f8 },
{9,	507,	    0xffffffff,	235        },
{9,	508,	    0xffffffff,	65278      },
{0}
	}
};

struct am_regs_s cmreg_optimize={
	172,
	{
/* optimize */
{REG_TYPE_INDEX_VPPCHROMA,	0x200,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x201,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x202,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x203,    0xffffffff, 0x8000400},
{REG_TYPE_INDEX_VPPCHROMA,	0x204,    0xffffffff, 0x90   },
{REG_TYPE_INDEX_VPPCHROMA,	0x205,    0xffffffff, 0x437077f},
{REG_TYPE_INDEX_VPPCHROMA,	0x206,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x207,    0xffffffff, 0x1f60000},
{REG_TYPE_INDEX_VPPCHROMA,	0x208,    0xffffffff, 0x76   },
{REG_TYPE_INDEX_VPPCHROMA,	0x209,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x20a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x20f,    0xf, 0x0,   },
{REG_TYPE_INDEX_VPP_COEF,	0x100,    0xffffffff, 0xa1100  },
{REG_TYPE_INDEX_VPP_COEF,	0x101,    0xffffffff, 0xc   },
{REG_TYPE_INDEX_VPP_COEF,	0x102,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x103,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x104,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x108,    0xffffffff, 0xd1100  },
{REG_TYPE_INDEX_VPP_COEF,	0x109,    0xffffffff, 0x1f   },
{REG_TYPE_INDEX_VPP_COEF,	0x10a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x10b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x10c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x110,    0xffffffff, 0xf1300  },
{REG_TYPE_INDEX_VPP_COEF,	0x111,    0xffffffff, 0x33   },
{REG_TYPE_INDEX_VPP_COEF,	0x112,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x113,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x114,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x118,    0xffffffff, 0xf1300  },
{REG_TYPE_INDEX_VPP_COEF,	0x119,    0xffffffff, 0x3f   },
{REG_TYPE_INDEX_VPP_COEF,	0x11a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x11b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x11c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x120,    0xffffffff, 0xf1300  },
{REG_TYPE_INDEX_VPP_COEF,	0x121,    0xffffffff, 0x40   },
{REG_TYPE_INDEX_VPP_COEF,	0x122,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x123,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x124,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x128,    0xffffffff, 0xf1100  },
{REG_TYPE_INDEX_VPP_COEF,	0x129,    0xffffffff, 0x40   },
{REG_TYPE_INDEX_VPP_COEF,	0x12a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x12b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x12c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x130,    0xffffffff, 0xd0f00  },
{REG_TYPE_INDEX_VPP_COEF,	0x131,    0xffffffff, 0x3f   },
{REG_TYPE_INDEX_VPP_COEF,	0x132,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x133,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x134,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x138,    0xffffffff, 0xa0d00  },
{REG_TYPE_INDEX_VPP_COEF,	0x139,    0xffffffff, 0x32   },
{REG_TYPE_INDEX_VPP_COEF,	0x13a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x13b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x13c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x140,    0xffffffff, 0x80a00  },
{REG_TYPE_INDEX_VPP_COEF,	0x141,    0xffffffff, 0x9   },
{REG_TYPE_INDEX_VPP_COEF,	0x142,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x143,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x144,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x148,    0xffffffff, 0x80a00  },
{REG_TYPE_INDEX_VPP_COEF,	0x149,    0xffffffff, 0xea   },
{REG_TYPE_INDEX_VPP_COEF,	0x14a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x14b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x14c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x150,    0xffffffff, 0xa0a00  },
{REG_TYPE_INDEX_VPP_COEF,	0x151,    0xffffffff, 0xd4   },
{REG_TYPE_INDEX_VPP_COEF,	0x152,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x153,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x154,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x158,    0xffffffff, 0xa0800  },
{REG_TYPE_INDEX_VPP_COEF,	0x159,    0xffffffff, 0xc3   },
{REG_TYPE_INDEX_VPP_COEF,	0x15a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x15b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x15c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x160,    0xffffffff, 0xa0800  },
{REG_TYPE_INDEX_VPP_COEF,	0x161,    0xffffffff, 0xc0   },
{REG_TYPE_INDEX_VPP_COEF,	0x162,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x163,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x164,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x168,    0xffffffff, 0x80800  },
{REG_TYPE_INDEX_VPP_COEF,	0x169,    0xffffffff, 0xcb   },
{REG_TYPE_INDEX_VPP_COEF,	0x16a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x16b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x16c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x170,    0xffffffff, 0x60800  },
{REG_TYPE_INDEX_VPP_COEF,	0x171,    0xffffffff, 0xd4   },
{REG_TYPE_INDEX_VPP_COEF,	0x172,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x173,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x174,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x178,    0xffffffff, 0x40600  },
{REG_TYPE_INDEX_VPP_COEF,	0x179,    0xffffffff, 0xdb   },
{REG_TYPE_INDEX_VPP_COEF,	0x17a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x17b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x17c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x180,    0xffffffff, 0x60600  },
{REG_TYPE_INDEX_VPP_COEF,	0x181,    0xffffffff, 0xea   },
{REG_TYPE_INDEX_VPP_COEF,	0x182,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x183,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x184,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x188,    0xffffffff, 0x60600  },
{REG_TYPE_INDEX_VPP_COEF,	0x189,    0xffffffff, 0xf7   },
{REG_TYPE_INDEX_VPP_COEF,	0x18a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x18b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x18c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x190,    0xffffffff, 0x60600  },
{REG_TYPE_INDEX_VPP_COEF,	0x191,    0xffffffff, 0x3   },
{REG_TYPE_INDEX_VPP_COEF,	0x192,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x193,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x194,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x198,    0xffffffff, 0x80800  },
{REG_TYPE_INDEX_VPP_COEF,	0x199,    0xffffffff, 0x9   },
{REG_TYPE_INDEX_VPP_COEF,	0x19a,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x19b,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x19c,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a0,    0xffffffff, 0x60a00  },
{REG_TYPE_INDEX_VPP_COEF,	0x1a1,    0xffffffff, 0xc   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a2,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a3,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a4,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a8,    0xffffffff, 0x60d00  },
{REG_TYPE_INDEX_VPP_COEF,	0x1a9,    0xffffffff, 0xe   },
{REG_TYPE_INDEX_VPP_COEF,	0x1aa,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ab,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ac,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b0,    0xffffffff, 0x80d00  },
{REG_TYPE_INDEX_VPP_COEF,	0x1b1,    0xffffffff, 0xc   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b2,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b3,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b4,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b8,    0xffffffff, 0xa0d00  },
{REG_TYPE_INDEX_VPP_COEF,	0x1b9,    0xffffffff, 0x7   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ba,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1bb,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1bc,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c0,    0xffffffff, 0xf0f00  },
{REG_TYPE_INDEX_VPP_COEF,	0x1c1,    0xffffffff, 0xfb   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c2,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c3,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c4,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c8,    0xffffffff, 0x110f00 },
{REG_TYPE_INDEX_VPP_COEF,	0x1c9,    0xffffffff, 0xf2   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ca,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1cb,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1cc,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d0,    0xffffffff, 0x131100 },
{REG_TYPE_INDEX_VPP_COEF,	0x1d1,    0xffffffff, 0xee   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d2,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d3,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d4,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d8,    0xffffffff, 0x131500 },
{REG_TYPE_INDEX_VPP_COEF,	0x1d9,    0xffffffff, 0xf5   },
{REG_TYPE_INDEX_VPP_COEF,	0x1da,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1db,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1dc,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e0,    0xffffffff, 0x151a00 },
{REG_TYPE_INDEX_VPP_COEF,	0x1e1,    0xffffffff, 0x1   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e2,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e3,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e4,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e8,    0xffffffff, 0x171c00 },
{REG_TYPE_INDEX_VPP_COEF,	0x1e9,    0xffffffff, 0x5   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ea,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1eb,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ec,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f0,    0xffffffff, 0x171a00 },
{REG_TYPE_INDEX_VPP_COEF,	0x1f1,    0xffffffff, 0x5   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f2,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f3,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f4,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f8,    0xffffffff, 0x131500 },
{REG_TYPE_INDEX_VPP_COEF,	0x1f9,    0xffffffff, 0x7   },
{REG_TYPE_INDEX_VPP_COEF,	0x1fa,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1fb,    0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1fc,    0xffffffff, 0x0   },
{0}
	}
};
struct am_regs_s cmreg_enhancement={
	172,
	{
/* enhancement */
{REG_TYPE_INDEX_VPPCHROMA,	0x200,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x201,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x202,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x203,	  0xffffffff, 0x8000400},
{REG_TYPE_INDEX_VPPCHROMA,	0x204,	  0xffffffff, 0x90   },
{REG_TYPE_INDEX_VPPCHROMA,	0x205,	  0xffffffff, 0x437077f},
{REG_TYPE_INDEX_VPPCHROMA,	0x206,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x207,	  0xffffffff, 0x2640000},
{REG_TYPE_INDEX_VPPCHROMA,	0x208,	  0xffffffff, 0x7a   },
{REG_TYPE_INDEX_VPPCHROMA,	0x209,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x20a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPPCHROMA,	0x20f,	  0xf, 0x0,   },
{REG_TYPE_INDEX_VPP_COEF,	0x100,	  0xffffffff, 0x2b5600 },
{REG_TYPE_INDEX_VPP_COEF,	0x101,	  0xffffffff, 0xc   },
{REG_TYPE_INDEX_VPP_COEF,	0x102,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x103,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x104,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x108,	  0xffffffff, 0x295600 },
{REG_TYPE_INDEX_VPP_COEF,	0x109,	  0xffffffff, 0x1f   },
{REG_TYPE_INDEX_VPP_COEF,	0x10a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x10b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x10c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x110,	  0xffffffff, 0x295600 },
{REG_TYPE_INDEX_VPP_COEF,	0x111,	  0xffffffff, 0x33   },
{REG_TYPE_INDEX_VPP_COEF,	0x112,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x113,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x114,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x118,	  0xffffffff, 0x2b5600 },
{REG_TYPE_INDEX_VPP_COEF,	0x119,	  0xffffffff, 0x3f   },
{REG_TYPE_INDEX_VPP_COEF,	0x11a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x11b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x11c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x120,	  0xffffffff, 0x2b5600 },
{REG_TYPE_INDEX_VPP_COEF,	0x121,	  0xffffffff, 0x3f   },
{REG_TYPE_INDEX_VPP_COEF,	0x122,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x123,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x124,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x128,	  0xffffffff, 0x295600 },
{REG_TYPE_INDEX_VPP_COEF,	0x129,	  0xffffffff, 0x3f   },
{REG_TYPE_INDEX_VPP_COEF,	0x12a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x12b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x12c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x130,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x131,	  0xffffffff, 0x3f   },
{REG_TYPE_INDEX_VPP_COEF,	0x132,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x133,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x134,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x138,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x139,	  0xffffffff, 0x33   },
{REG_TYPE_INDEX_VPP_COEF,	0x13a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x13b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x13c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x140,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x141,	  0xffffffff, 0x9   },
{REG_TYPE_INDEX_VPP_COEF,	0x142,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x143,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x144,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x148,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x149,	  0xffffffff, 0xea   },
{REG_TYPE_INDEX_VPP_COEF,	0x14a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x14b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x14c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x150,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x151,	  0xffffffff, 0xd4   },
{REG_TYPE_INDEX_VPP_COEF,	0x152,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x153,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x154,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x158,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x159,	  0xffffffff, 0xc5   },
{REG_TYPE_INDEX_VPP_COEF,	0x15a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x15b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x15c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x160,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x161,	  0xffffffff, 0xc1   },
{REG_TYPE_INDEX_VPP_COEF,	0x162,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x163,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x164,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x168,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x169,	  0xffffffff, 0xcb   },
{REG_TYPE_INDEX_VPP_COEF,	0x16a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x16b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x16c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x170,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x171,	  0xffffffff, 0xd6   },
{REG_TYPE_INDEX_VPP_COEF,	0x172,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x173,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x174,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x178,	  0xffffffff, 0x295600 },
{REG_TYPE_INDEX_VPP_COEF,	0x179,	  0xffffffff, 0xdb   },
{REG_TYPE_INDEX_VPP_COEF,	0x17a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x17b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x17c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x180,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x181,	  0xffffffff, 0xec   },
{REG_TYPE_INDEX_VPP_COEF,	0x182,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x183,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x184,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x188,	  0xffffffff, 0x295600 },
{REG_TYPE_INDEX_VPP_COEF,	0x189,	  0xffffffff, 0xf9   },
{REG_TYPE_INDEX_VPP_COEF,	0x18a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x18b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x18c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x190,	  0xffffffff, 0x295600 },
{REG_TYPE_INDEX_VPP_COEF,	0x191,	  0xffffffff, 0x3   },
{REG_TYPE_INDEX_VPP_COEF,	0x192,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x193,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x194,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x198,	  0xffffffff, 0x295600 },
{REG_TYPE_INDEX_VPP_COEF,	0x199,	  0xffffffff, 0x9   },
{REG_TYPE_INDEX_VPP_COEF,	0x19a,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x19b,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x19c,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a0,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x1a1,	  0xffffffff, 0xc   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a2,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a3,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a4,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1a8,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x1a9,	  0xffffffff, 0xe   },
{REG_TYPE_INDEX_VPP_COEF,	0x1aa,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ab,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ac,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b0,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x1b1,	  0xffffffff, 0xc   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b2,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b3,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b4,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1b8,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x1b9,	  0xffffffff, 0x7   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ba,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1bb,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1bc,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c0,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x1c1,	  0xffffffff, 0xfb   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c2,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c3,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c4,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1c8,	  0xffffffff, 0x295400 },
{REG_TYPE_INDEX_VPP_COEF,	0x1c9,	  0xffffffff, 0xf2   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ca,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1cb,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1cc,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d0,	  0xffffffff, 0x2295400},
{REG_TYPE_INDEX_VPP_COEF,	0x1d1,	  0xffffffff, 0xee   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d2,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d3,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d4,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1d8,	  0xffffffff, 0x4295400},
{REG_TYPE_INDEX_VPP_COEF,	0x1d9,	  0xffffffff, 0xf5   },
{REG_TYPE_INDEX_VPP_COEF,	0x1da,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1db,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1dc,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e0,	  0xffffffff, 0x8295400},
{REG_TYPE_INDEX_VPP_COEF,	0x1e1,	  0xffffffff, 0x1   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e2,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e3,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e4,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1e8,	  0xffffffff, 0x8295400},
{REG_TYPE_INDEX_VPP_COEF,	0x1e9,	  0xffffffff, 0x5   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ea,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1eb,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1ec,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f0,	  0xffffffff, 0x8295400},
{REG_TYPE_INDEX_VPP_COEF,	0x1f1,	  0xffffffff, 0x5   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f2,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f3,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f4,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1f8,	  0xffffffff, 0x4295400},
{REG_TYPE_INDEX_VPP_COEF,	0x1f9,	  0xffffffff, 0x7   },
{REG_TYPE_INDEX_VPP_COEF,	0x1fa,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1fb,	  0xffffffff, 0x0   },
{REG_TYPE_INDEX_VPP_COEF,	0x1fc,	  0xffffffff, 0x0   },
{0}
	}
};

module_param(video_rgb_ogo_mode_sw, int, 0664);
MODULE_PARM_DESC(video_rgb_ogo_mode_sw, "enable/disable video_rgb_ogo_mode_sw");

static int frame_lock_freq = 0;
module_param(frame_lock_freq, int, 0664);
MODULE_PARM_DESC(frame_lock_freq, "frame_lock_50");

/*
* set the frame size for cm2 demo
*/
static void cm2_frame_switch_patch(void)
{
    WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, 0x20f);
    WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, cm2_patch_flag);
}
#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD)
static void ve_frame_size_patch(unsigned int width,unsigned int height)
{
    unsigned int vpp_size = height|(width << 16);
    if(ve_size != vpp_size){
	WRITE_CBUS_REG(VPP_VE_H_V_SIZE, vpp_size);
	ve_size = vpp_size;
    }
}
#endif
static void cm2_frame_size_patch(unsigned int width,unsigned int height)
{
    unsigned int vpp_size;
    /*check if the cm2 enable/disable to config the cm2 size*/
    if(!(READ_CBUS_REG(VPP_MISC)&(0x1<<28)))
        return;

    vpp_size = width|(height << 16);
    if(cm_size == 0){
         WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, 0x205);
         cm_size = READ_CBUS_REG(VPP_CHROMA_DATA_PORT);
    }
    if (cm_size != vpp_size) {
        WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, 0x205);
        WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, vpp_size);
        WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, 0x209);
        WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, width<<15);
        WRITE_CBUS_REG(VPP_CHROMA_ADDR_PORT, 0x20a);
        WRITE_CBUS_REG(VPP_CHROMA_DATA_PORT, height<<16);
        cm_size =  vpp_size;
    }
#ifdef PQ_DEBUG_EN
	printk("\n[amvecm..]cm2_frame_patch: set cm2 framesize %x, set demo mode  %x\n",vpp_size, cm2_patch_flag);
#endif
}

void amvecm_video_latch(void)
{
    unsigned int hs, he, vs, ve;

    if ((vecm_latch_flag & FLAG_REG_MAP0) ||
    	(vecm_latch_flag & FLAG_REG_MAP1) ||
    	(vecm_latch_flag & FLAG_REG_MAP2) ||
    	(vecm_latch_flag & FLAG_REG_MAP3) ||
    	(vecm_latch_flag & FLAG_REG_MAP4) ||
    	(vecm_latch_flag & FLAG_REG_MAP5)
       )
    {
    	if (vecm_latch_flag & FLAG_REG_MAP0) {
            am_set_regmap(&amregs0);
            vecm_latch_flag &= ~FLAG_REG_MAP0;
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] load reg 0 table OK!!!\n");
#endif
    	}
    	if (vecm_latch_flag & FLAG_REG_MAP1) {
            am_set_regmap(&amregs1);
            vecm_latch_flag &= ~FLAG_REG_MAP1;
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] load reg 1 table OK!!!\n");
#endif
    	}
    	if (vecm_latch_flag & FLAG_REG_MAP2) {
            am_set_regmap(&amregs2);
            vecm_latch_flag &= ~FLAG_REG_MAP2;
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] load reg 2 table OK!!!\n");
#endif
    	}
    	if (vecm_latch_flag & FLAG_REG_MAP3) {
            am_set_regmap(&amregs3);
            vecm_latch_flag &= ~FLAG_REG_MAP3;
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] load reg 3 table OK!!!\n");
#endif
    	}
    	if (vecm_latch_flag & FLAG_REG_MAP4) {
            am_set_regmap(&amregs4);
            vecm_latch_flag &= ~FLAG_REG_MAP4;
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] load reg 4 table OK!!!\n");
#endif
    	}
    	if (vecm_latch_flag & FLAG_REG_MAP5) {
            am_set_regmap(&amregs5);
            vecm_latch_flag &= ~FLAG_REG_MAP5;
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] load reg 5 table OK!!!\n");
#endif
    	}

	if((cm2_patch_flag & 0xff) > 0)
	    cm2_frame_switch_patch();
    }
    hs = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_H_START_END,16,12);
    he = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_H_START_END,0,12);

    vs = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_V_START_END,16,12);
    ve = READ_CBUS_REG_BITS(VPP_POSTBLEND_VD1_V_START_END,0,12);
#if ((MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8)||(MESON_CPU_TYPE==MESON_CPU_TYPE_MESON8B))
    if(cm_en)
#endif
        cm2_frame_size_patch(he-hs+1,ve-vs+1);
#if (MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD)
    ve_frame_size_patch(he-hs+1,ve-vs+1);
#endif
    if (vecm_latch_flag & FLAG_VE_DNLP)
    {
        vecm_latch_flag &= ~FLAG_VE_DNLP;
        ve_set_dnlp(&am_ve_dnlp);
    }
    if (vecm_latch_flag & FLAG_VE_NEW_DNLP)
    {
        vecm_latch_flag &= ~FLAG_VE_NEW_DNLP;
        ve_set_new_dnlp(&am_ve_new_dnlp);
    }
	if (vecm_latch_flag & FLAG_VE_DNLP_EN)
	{
		vecm_latch_flag &= ~FLAG_VE_DNLP_EN;
		ve_enable_dnlp();
#ifdef PQ_DEBUG_EN
		printk("\n[amvecm..] set vpp_enable_dnlp OK!!!\n");
#endif
	}
	if (vecm_latch_flag & FLAG_VE_DNLP_DIS)
	{
		vecm_latch_flag &= ~FLAG_VE_DNLP_DIS;
		ve_disable_dnlp();
#ifdef PQ_DEBUG_EN
		printk("\n[amvecm..] set vpp_disable_dnlp OK!!!\n");
#endif
	}
    if (vecm_latch_flag & FLAG_GAMMA_TABLE_EN)
    {
        vecm_latch_flag &= ~FLAG_GAMMA_TABLE_EN;
        vpp_enable_lcd_gamma_table();
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] set vpp_enable_lcd_gamma_table OK!!!\n");
#endif
    }
    if (vecm_latch_flag & FLAG_GAMMA_TABLE_DIS)
    {
        vecm_latch_flag &= ~FLAG_GAMMA_TABLE_DIS;
        vpp_disable_lcd_gamma_table();
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] set vpp_disable_lcd_gamma_table OK!!!\n");
#endif
    }
    if (vecm_latch_flag & FLAG_GAMMA_TABLE_R)
    {
        vecm_latch_flag &= ~FLAG_GAMMA_TABLE_R;
        vpp_set_lcd_gamma_table(video_gamma_table_r.data, H_SEL_R);
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] set vpp_set_lcd_gamma_table OK!!!\n");
#endif
    }
    if (vecm_latch_flag & FLAG_GAMMA_TABLE_G)
    {
        vecm_latch_flag &= ~FLAG_GAMMA_TABLE_G;
        vpp_set_lcd_gamma_table(video_gamma_table_g.data, H_SEL_G);
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] set vpp_set_lcd_gamma_table OK!!!\n");
#endif
    }
    if (vecm_latch_flag & FLAG_GAMMA_TABLE_B)
    {
        vecm_latch_flag &= ~FLAG_GAMMA_TABLE_B;
        vpp_set_lcd_gamma_table(video_gamma_table_b.data, H_SEL_B);
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] set vpp_set_lcd_gamma_table OK!!!\n");
#endif
    }
    if (vecm_latch_flag & FLAG_RGB_OGO)
    {
        vecm_latch_flag &= ~FLAG_RGB_OGO;
        if (video_rgb_ogo_mode_sw)
        {
            if (video_rgb_ogo.en)
            {
                vpp_set_lcd_gamma_table(video_gamma_table_r_adj.data, H_SEL_R);
                vpp_set_lcd_gamma_table(video_gamma_table_g_adj.data, H_SEL_G);
                vpp_set_lcd_gamma_table(video_gamma_table_b_adj.data, H_SEL_B);
            }
            else
            {
                vpp_set_lcd_gamma_table(video_gamma_table_r.data, H_SEL_R);
                vpp_set_lcd_gamma_table(video_gamma_table_g.data, H_SEL_G);
                vpp_set_lcd_gamma_table(video_gamma_table_b.data, H_SEL_B);
            }
	#ifdef PQ_DEBUG_EN
            printk("\n[amvecm..] set vpp_set_lcd_gamma_table OK!!!\n");
	#endif
        }
        else
        {
            vpp_set_rgb_ogo(&video_rgb_ogo);
	#ifdef PQ_DEBUG_EN
            printk("\n[amvecm..] set vpp_set_rgb_ogo OK!!!\n");
	#endif
        }
    }
    if (vecm_latch_flag & FLAG_BRI_CON)
    {
        vecm_latch_flag &= ~FLAG_BRI_CON;
        vd1_brightness_contrast(vd1_brightness, vd1_contrast);
#ifdef PQ_DEBUG_EN
        printk("\n[amvecm..] set vd1_brightness_contrast OK!!!\n");
#endif
    }
    if (cm_en&&(cm_level_last != cm_level))
    {
    	cm_level_last = cm_level;
    	WRITE_CBUS_REG_BITS(VPP_MISC,1,28,1);//CM manage enable
    	if(cm_level == 1)
    		am_set_regmap(&cmreg_lever1);
	else if(cm_level == 2)
		am_set_regmap(&cmreg_lever2);
	else if(cm_level == 3)
		am_set_regmap(&cmreg_lever3);
	else if(cm_level == 4)
		am_set_regmap(&cmreg_enhancement);
	else
		am_set_regmap(&cmreg_optimize);
#if 1//def PQ_DEBUG_EN
        printk("\n[amvecm..] set cm2 load OK!!!\n");
#endif
    }
    else if(cm_en == 0){
    	cm_level_last = 0xff;
    	WRITE_CBUS_REG_BITS(VPP_MISC,0,28,1);//CM manage enable
    }
    if(dnlp_en&&dnlp_status)
    {
	dnlp_status = 0;
	ve_set_dnlp_2();
	ve_enable_dnlp();
#ifdef PQ_DEBUG_EN
	printk("\n[amvecm..] set vpp_enable_dnlp OK!!!\n");
#endif
    }
    else if(dnlp_en == 0){
    	dnlp_status = 1;
    	ve_disable_dnlp();
#ifdef PQ_DEBUG_EN
	printk("\n[amvecm..] set vpp_disable_dnlp OK!!!\n");
#endif
    }
#if ((MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TV)||(MESON_CPU_TYPE==MESON_CPU_TYPE_MESON6TVD))
    /* lvds freq 50Hz/60Hz */
    if (frame_lock_freq == 1)  //50 hz
    {
        // panel freq is 60Hz => change back to 50Hz
        if (READ_MPEG_REG(ENCP_VIDEO_MAX_LNCNT) < 1237) // (1124 + 1349 +1) / 2
        {
            WRITE_MPEG_REG(ENCP_VIDEO_MAX_LNCNT, 1349);
        }
    }
    else if (frame_lock_freq == 2)  //60 hz
    {
        // panel freq is 50Hz => change back to 60Hz
        if (READ_MPEG_REG(ENCP_VIDEO_MAX_LNCNT) >= 1237) // (1124 + 1349 + 1) / 2
        {
            WRITE_MPEG_REG(ENCP_VIDEO_MAX_LNCNT, 1124);
        }
    }
    else if (frame_lock_freq == 0)
    {
        /* lvds freq 50Hz/60Hz */
        if (vecm_latch_flag & FLAG_LVDS_FREQ_SW)  //50 hz
        {
            // panel freq is 60Hz => change back to 50Hz
            if (READ_MPEG_REG(ENCP_VIDEO_MAX_LNCNT) < 1237) // (1124 + 1349 +1) / 2
            {
                WRITE_MPEG_REG(ENCP_VIDEO_MAX_LNCNT, 1349);
            }
        }
        else   //60 hz
        {
            // panel freq is 50Hz => change back to 60Hz
            if (READ_MPEG_REG(ENCP_VIDEO_MAX_LNCNT) >= 1237) // (1124 + 1349 + 1) / 2
            {
                WRITE_MPEG_REG(ENCP_VIDEO_MAX_LNCNT, 1124);
            }
        }
    }
#endif

}
EXPORT_SYMBOL(amvecm_video_latch);

/*
amvecm device driver
*/
struct tcon_gamma_table_s video_curve_2d2_inv =
{
	{
	   0,	82,  113,  136,  155,  171,  186,  199,  212,  223,  234,  245,  255,  264,  273,  282,
	 290,  298,  306,  314,  321,  328,  335,  342,  349,  356,  362,  368,  374,  380,  386,  392,
	 398,  403,  409,  414,  420,  425,  430,  435,  440,  445,  450,  455,  460,  464,  469,  474,
	 478,  483,  487,  492,  496,  500,  505,  509,  513,  517,  521,  525,  529,  533,  537,  541,
	 545,  549,  553,  556,  560,  564,  568,  571,  575,  579,  582,  586,  589,  593,  596,  600,
	 603,  607,  610,  613,  617,  620,  623,  627,  630,  633,  636,  640,  643,  646,  649,  652,
	 655,  658,  661,  665,  668,  671,  674,  677,  680,  683,  686,  688,  691,  694,  697,  700,
	 703,  706,  709,  711,  714,  717,  720,  723,  725,  728,  731,  733,  736,  739,  742,  744,
	 747,  750,  752,  755,  757,  760,  763,  765,  768,  770,  773,  775,  778,  780,  783,  785,
	 788,  790,  793,  795,  798,  800,  803,  805,  808,  810,  812,  815,  817,  820,  822,  824,
	 827,  829,  831,  834,  836,  838,  841,  843,  845,  847,  850,  852,  854,  856,  859,  861,
	 863,  865,  868,  870,  872,  874,  876,  879,  881,  883,  885,  887,  889,  892,  894,  896,
	 898,  900,  902,  904,  906,  909,  911,  913,  915,  917,  919,  921,  923,  925,  927,  929,
	 931,  933,  935,  937,  939,  941,  943,  945,  947,  949,  951,  953,  955,  957,  959,  961,
	 963,  965,  967,  969,  971,  973,  975,  977,  979,  981,  982,  984,  986,  988,  990,  992,
	 994,  996,  998,  999, 1001, 1003, 1005, 1007, 1009, 1011, 1012, 1014, 1016, 1018, 1020, 1022,
	},
};

struct tcon_gamma_table_s video_curve_2d2 =
{
	{
	   0,	 0,    0,	 0,    0,	 0,    0,	 0,    1,	 1,    1,	 1,    1,	 1,    2,	 2,
	   2,	 3,    3,	 3,    4,	 4,    5,	 5,    6,	 6,    7,	 7,    8,	 9,    9,	10,
	  11,	11,   12,	13,   14,	15,   15,	16,   17,	18,   19,	20,   21,	22,   23,	25,
	  26,	27,   28,	29,   31,	32,   33,	35,   36,	38,   39,	41,   42,	44,   45,	47,
	  49,	50,   52,	54,   55,	57,   59,	61,   63,	65,   67,	69,   71,	73,   75,	77,
	  79,	82,   84,	86,   88,	91,   93,	95,   98,  100,  103,  105,  108,  110,  113,  116,
	 118,  121,  124,  127,  130,  132,  135,  138,  141,  144,  147,  150,  154,  157,  160,  163,
	 166,  170,  173,  176,  180,  183,  187,  190,  194,  197,  201,  204,  208,  212,  216,  219,
	 223,  227,  231,  235,  239,  243,  247,  251,  255,  259,  263,  267,  272,  276,  280,  285,
	 289,  294,  298,  303,  307,  312,  316,  321,  326,  330,  335,  340,  345,  350,  355,  360,
	 365,  370,  375,  380,  385,  390,  395,  401,  406,  411,  417,  422,  427,  433,  438,  444,
	 450,  455,  461,  467,  472,  478,  484,  490,  496,  502,  508,  514,  520,  526,  532,  538,
	 544,  551,  557,  563,  570,  576,  583,  589,  596,  602,  609,  615,  622,  629,  636,  642,
	 649,  656,  663,  670,  677,  684,  691,  698,  705,  713,  720,  727,  735,  742,  749,  757,
	 764,  772,  779,  787,  795,  802,  810,  818,  826,  833,  841,  849,  857,  865,  873,  881,
	 889,  898,  906,  914,  922,  931,  939,  948,  956,  965,  973,  982,  990,  999, 1008, 1016,
	},
};

static void video_data_limitation(int *val)
{
	if (*val > 1023)
		*val = 1023;
	if (*val <	  0)
		*val =	  0;
}

static void video_lookup(struct tcon_gamma_table_s *tbl, int *val)
{
	unsigned int idx = (*val) >> 2, mod = (*val) & 3;

	if (idx < 255)
		*val = tbl->data[idx] + (((tbl->data[idx + 1] - tbl->data[idx]) * mod + 2) >> 2);
	else
		*val = tbl->data[idx] + (((1023 			  - tbl->data[idx]) * mod + 2) >> 2);
}

static void video_set_rgb_ogo(void)
{
	int i = 0, r = 0, g = 0, b = 0;

	for (i = 0; i < 256; i++)
	{
		// Get curve_straight = input(curve_2d2_inv) * video_curve_2d2
		r = video_curve_2d2.data[i];
		g = video_curve_2d2.data[i];
		b = video_curve_2d2.data[i];
		// Pre_offset
		r += video_rgb_ogo.r_pre_offset;
		g += video_rgb_ogo.g_pre_offset;
		b += video_rgb_ogo.b_pre_offset;
		video_data_limitation(&r);
		video_data_limitation(&g);
		video_data_limitation(&b);
		// Gain
		r  *= video_rgb_ogo.r_gain;
		r >>= 10;
		g  *= video_rgb_ogo.g_gain;
		g >>= 10;
		b  *= video_rgb_ogo.b_gain;
		b >>= 10;
		video_data_limitation(&r);
		video_data_limitation(&g);
		video_data_limitation(&b);
		// Post_offset
		r += video_rgb_ogo.r_post_offset;
		g += video_rgb_ogo.g_post_offset;
		b += video_rgb_ogo.b_post_offset;
		video_data_limitation(&r);
		video_data_limitation(&g);
		video_data_limitation(&b);
		// Get curve_2d2_inv_ogo = curve_straight_ogo * video_curve_2d2_inv
		video_lookup(&video_curve_2d2_inv, &r);
		video_lookup(&video_curve_2d2_inv, &g);
		video_lookup(&video_curve_2d2_inv, &b);
		// Get gamma_ogo = curve_2d2_inv_ogo * gamma
		video_lookup(&video_gamma_table_r, &r);
		video_lookup(&video_gamma_table_g, &g);
		video_lookup(&video_gamma_table_b, &b);
		// Save gamma_ogo
		video_gamma_table_r_adj.data[i] = r;
		video_gamma_table_g_adj.data[i] = g;
		video_gamma_table_b_adj.data[i] = b;
	}
}

void vd1_brightness_contrast(signed int brightness, signed int contrast)
{
      signed int ao0 =  -64, g00 = 1024, g01 =    0, g02 =    0, po0 =  64;
      signed int ao1 = -512, g10 =    0, g11 = 1024, g12 =    0, po1 = 512;
      signed int ao2 = -512, g20 =    0, g21 =    0, g22 = 1024, po2 = 512;
    unsigned int gc0 =    0, gc1 =    0, gc2 =    0, gc3 =    0, gc4 =   0;
    unsigned int a01 =    0, a_2 =    0, p01 =    0, p_2 =    0;
    // enable vd0_csc
    unsigned int ori = READ_CBUS_REG(VPP_MATRIX_CTRL) | 0x00000020;
    // point to vd0_csc
    unsigned int ctl = (ori & 0xfffffcff) | 0x00000100;

    po0 += brightness >> 1;
    if (po0 >  1023)
        po0 =  1023;
    if (po0 < -1024)
        po0 = -1024;

    g00  *= contrast + 2048;
    g00 >>= 11;
    if (g00 >  4095)
        g00 =  4095;
    if (g00 < -4096)
        g00 = -4096;

    if (contrast < 0)
    {
        g11  *= contrast   + 2048;
        g11 >>= 11;
    }

    if (brightness < 0)
    {
        g11  += brightness >> 1;
        if (g11 >  4095)
            g11 =  4095;
        if (g11 < -4096)
            g11 = -4096;
    }

    if (contrast < 0)
    {
        g22  *= contrast   + 2048;
        g22 >>= 11;
    }

    if (brightness < 0)
    {
        g22  += brightness >> 1;
        if (g22 >  4095)
            g22 =  4095;
        if (g22 < -4096)
            g22 = -4096;
    }

    gc0 = ((g00 << 16) & 0x1fff0000) |
          ((g01 <<  0) & 0x00001fff);
    gc1 = ((g02 << 16) & 0x1fff0000) |
          ((g10 <<  0) & 0x00001fff);
    gc2 = ((g11 << 16) & 0x1fff0000) |
          ((g12 <<  0) & 0x00001fff);
    gc3 = ((g20 << 16) & 0x1fff0000) |
          ((g21 <<  0) & 0x00001fff);
    gc4 = ((g22 <<  0) & 0x00001fff);
    a01 = ((ao0 << 16) & 0x07ff0000) |
          ((ao1 <<  0) & 0x000007ff);
    a_2 = ((ao2 <<  0) & 0x000007ff);
    p01 = ((po0 << 16) & 0x07ff0000) |
          ((po1 <<  0) & 0x000007ff);
    p_2 = ((po2 <<  0) & 0x000007ff);

    WRITE_CBUS_REG(VPP_MATRIX_CTRL         , ctl);
    WRITE_CBUS_REG(VPP_MATRIX_COEF00_01    , gc0);
    WRITE_CBUS_REG(VPP_MATRIX_COEF02_10    , gc1);
    WRITE_CBUS_REG(VPP_MATRIX_COEF11_12    , gc2);
    WRITE_CBUS_REG(VPP_MATRIX_COEF20_21    , gc3);
    WRITE_CBUS_REG(VPP_MATRIX_COEF22       , gc4);
    WRITE_CBUS_REG(VPP_MATRIX_PRE_OFFSET0_1, a01);
    WRITE_CBUS_REG(VPP_MATRIX_PRE_OFFSET2  , a_2);
    WRITE_CBUS_REG(VPP_MATRIX_OFFSET0_1    , p01);
    WRITE_CBUS_REG(VPP_MATRIX_OFFSET2      , p_2);
    WRITE_CBUS_REG(VPP_MATRIX_CTRL         , ori);
}

static int amvecm_open(struct inode *inode, struct file *file)
{
    amvecm_dev_t *devp;

    /* Get the per-device structure that contains this cdev */
    devp = container_of(inode->i_cdev, amvecm_dev_t, cdev);
    file->private_data = devp;

    return 0;
}

static int amvecm_release(struct inode *inode, struct file *file)
{
    //amvecm_dev_t *devp = file->private_data;

    file->private_data = NULL;

    return 0;
}

static long amvecm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    switch (cmd)
    {
		case AMVECM_IOC_LOAD_REG: {
#if 1
		    if ((vecm_latch_flag & FLAG_REG_MAP0) &&
		    	(vecm_latch_flag & FLAG_REG_MAP1) &&
		    	(vecm_latch_flag & FLAG_REG_MAP2) &&
		    	(vecm_latch_flag & FLAG_REG_MAP3) &&
		    	(vecm_latch_flag & FLAG_REG_MAP4) &&
		    	(vecm_latch_flag & FLAG_REG_MAP5)
		       ) {
		           ret = -EBUSY;
			   printk(KERN_ERR "[amvecm..] load regs error: loading regs, please wait\n");
			   goto out;
		    }
		    ret = -EFAULT;
                    /*force set cm size to 0,enable check vpp size*/
                    cm_size = 0;
		    if (!(vecm_latch_flag & FLAG_REG_MAP0)) {
		        if (copy_from_user(&amregs0, (void __user *)arg, sizeof(struct am_regs_s))) {
				printk(KERN_ERR "[amvecm..]0 load reg errors: can't get buffer lenght\n");
				goto out;
			}
			ret = -EINVAL;
			if (!amregs0.length || (amregs0.length > 512)) {
				printk(KERN_ERR "[amvecm..]0 load regs error: buffer length overflow!!!, length=0x%x \n",
					amregs0.length);
				goto out;
			}
#ifdef PQ_DEBUG_EN
			printk("\n[amvecm..]0 reg length=0x%x ......\n", amregs0.length);
#endif
			vecm_latch_flag |= FLAG_REG_MAP0;
		        ret = 0;
			goto out;
		    }
		    if (!(vecm_latch_flag & FLAG_REG_MAP1)) {
		        if (copy_from_user(&amregs1, (void __user *)arg, sizeof(struct am_regs_s))) {
				printk(KERN_ERR "[amvecm..]1 load reg errors: can't get buffer lenght\n");
				goto out;
			}
			ret = -EINVAL;
			if (!amregs1.length || (amregs1.length > 512)) {
				printk(KERN_ERR "[amvecm..]1 load regs error: buffer length overflow!!!, length=0x%x \n",
					amregs1.length);
				goto out;
			}
#ifdef PQ_DEBUG_EN
			printk("\n[amvecm..]1 reg length=0x%x ......\n", amregs1.length);
#endif
			vecm_latch_flag |= FLAG_REG_MAP1;
		        ret = 0;
			goto out;
		    }
		    if (!(vecm_latch_flag & FLAG_REG_MAP2)) {
		        if (copy_from_user(&amregs2, (void __user *)arg, sizeof(struct am_regs_s))) {
				printk(KERN_ERR "[amvecm..]2 load reg errors: can't get buffer lenght\n");
				goto out;
			}
			ret = -EINVAL;
			if (!amregs2.length || (amregs2.length > 512)) {
				printk(KERN_ERR "[amvecm..]2 load regs error: buffer length overflow!!!, length=0x%x \n",
					amregs2.length);
				goto out;
			}
#ifdef PQ_DEBUG_EN
			printk("\n[amvecm..]2 reg length=0x%x ......\n", amregs1.length);
#endif
			vecm_latch_flag |= FLAG_REG_MAP2;
		        ret = 0;
			goto out;
		    }
		    if (!(vecm_latch_flag & FLAG_REG_MAP3)) {
		        if (copy_from_user(&amregs3, (void __user *)arg, sizeof(struct am_regs_s))) {
				printk(KERN_ERR "[amvecm..]3 load reg errors: can't get buffer lenght\n");
				goto out;
			}
			ret = -EINVAL;
			if (!amregs3.length || (amregs3.length > 512)) {
				printk(KERN_ERR "[amvecm..]3 load regs error: buffer length overflow!!!, length=0x%x \n",
					amregs3.length);
				goto out;
			}
#ifdef PQ_DEBUG_EN
			printk("\n[amvecm..]3 reg length=0x%x ......\n", amregs3.length);
#endif
			vecm_latch_flag |= FLAG_REG_MAP3;
		        ret = 0;
			goto out;
		    }
		    if (!(vecm_latch_flag & FLAG_REG_MAP4)) {
		        if (copy_from_user(&amregs4, (void __user *)arg, sizeof(struct am_regs_s))) {
				printk(KERN_ERR "[amvecm..]4 load reg errors: can't get buffer lenght\n");
				goto out;
			}
			ret = -EINVAL;
			if (!amregs4.length || (amregs4.length > 512)) {
				printk(KERN_ERR "[amvecm..]4 load regs error: buffer length overflow!!!, length=0x%x \n",
					amregs4.length);
				goto out;
			}
#ifdef PQ_DEBUG_EN
			printk("\n[amvecm..]4 reg length=0x%x ......\n", amregs1.length);
#endif
			vecm_latch_flag |= FLAG_REG_MAP4;
		        ret = 0;
			goto out;
		    }
		    if (!(vecm_latch_flag & FLAG_REG_MAP5)) {
		        if (copy_from_user(&amregs5, (void __user *)arg, sizeof(struct am_regs_s))) {
				printk(KERN_ERR "[amvecm..]5 load reg errors: can't get buffer lenght\n");
				goto out;
			}
			ret = -EINVAL;
			if (!amregs5.length || (amregs5.length > 512)) {
				printk(KERN_ERR "[amvecm..]5 load regs error: buffer length overflow!!!, length=0x%x \n",
					amregs5.length);
				goto out;
			}
#ifdef PQ_DEBUG_EN
			printk("\n[amvecm..]5 reg length=0x%x ......\n", amregs5.length);
#endif
			vecm_latch_flag |= FLAG_REG_MAP5;
		        ret = 0;
			goto out;
		    }

#else
			if (vecm_latch_flag & FLAG_REG_MAP)
			{
			    if (hold_cmd_en) {
			        while (vecm_latch_flag & FLAG_REG_MAP) {
                #ifdef PQ_DEBUG_EN
			            printk(KERN_ERR "[amvecm..] command busy, please wait!!!\n");
                #else
			            ;
                #endif
			         }
			     } else {
			         ret = -EBUSY;
			         printk(KERN_ERR "[amvecm..] load regs error: loading regs, please wait\n");
			         goto out;
			     }
			}
			ret = -EFAULT;
			if (copy_from_user(&amregs, (void __user *)arg, sizeof(struct am_regs_s))) {
				printk(KERN_ERR "[amvecm..] load reg errors: can't get buffer lenght\n");
				goto out;
			}
			ret = -EINVAL;
			if (!amregs.length || (amregs.length > 512)) {
				printk(KERN_ERR "[amvecm..] load regs error: buffer length overflow!!!, length=0x%x \n", amregs.length);
				goto out;
			}
#ifdef PQ_DEBUG_EN
			printk("\n[amvecm..] reg length=0x%x ......\n", amregs.length);
#endif
			vecm_latch_flag |= FLAG_REG_MAP;
			ret = 0;
#endif
		out:
			break;
		}

        case AMVECM_IOC_VE_DNLP_EN: {
            if (vecm_latch_flag & FLAG_VE_DNLP_EN)
            {
                ret = -EFAULT;
            }
            else
            {
                vecm_latch_flag |= FLAG_VE_DNLP_EN;
                printk(KERN_ERR "[amvecm..] ioctl: enable dnlp function\n");
            }
            break;
        }

        case AMVECM_IOC_VE_DNLP_DIS: {
            if (vecm_latch_flag & FLAG_VE_DNLP_DIS)
            {
                ret = -EFAULT;
            }
            else
            {
                vecm_latch_flag |= FLAG_VE_DNLP_DIS;
                printk(KERN_ERR "[amvecm..] ioctl: disenable dnlp function\n");
            }
            break;
        }

        case AMVECM_IOC_VE_DNLP:
        {
            if (vecm_latch_flag & FLAG_VE_DNLP)
            {
                ret = -EFAULT;
            }
            else if (copy_from_user(&am_ve_dnlp, (void __user *)arg, sizeof(struct ve_dnlp_s)))
            {
                ret = -EFAULT;
            }
            else
            {
                if (am_ve_dnlp.en    >  1)
                    am_ve_dnlp.en    =  1;
                if (am_ve_dnlp.black > 16)
                    am_ve_dnlp.black = 16;
                if (am_ve_dnlp.white > 16)
                    am_ve_dnlp.white = 16;
                vecm_latch_flag |= FLAG_VE_DNLP;
            }
            break;
        }

		case AMVECM_IOC_VE_NEW_DNLP:
        {
            if (vecm_latch_flag & FLAG_VE_NEW_DNLP)
            {
                ret = -EFAULT;
            }
            else if (copy_from_user(&am_ve_new_dnlp, (void __user *)arg, sizeof(struct ve_dnlp_table_s)))
            {
                ret = -EFAULT;
            }
            else
            {
                if (am_ve_new_dnlp.en    >  1)
                    am_ve_new_dnlp.en    =  1;
                if (am_ve_new_dnlp.cliprate> 256)
                    am_ve_new_dnlp.cliprate= 256;
                if (am_ve_new_dnlp.lowrange> 54)
                    am_ve_new_dnlp.lowrange= 54;
                if (am_ve_new_dnlp.hghrange> 54)
                    am_ve_new_dnlp.hghrange= 54;
                if (am_ve_new_dnlp.lowalpha> 48)
                    am_ve_new_dnlp.lowalpha= 48;
                if (am_ve_new_dnlp.midalpha> 48)
                    am_ve_new_dnlp.midalpha= 48;
                if (am_ve_new_dnlp.hghalpha> 48)
                    am_ve_new_dnlp.hghalpha= 48;
                vecm_latch_flag |= FLAG_VE_NEW_DNLP;
            }
            break;
        }

        case AMVECM_IOC_G_HIST_AVG: {
            void __user *argp = (void __user *)arg;
            if ((video_ve_hist.height == 0) || (video_ve_hist.width == 0))
            {
                ret = -EFAULT;
            }
            else
            {
                video_ve_hist.ave = video_ve_hist.sum/(video_ve_hist.height*video_ve_hist.width);
                if (copy_to_user(argp, &video_ve_hist, sizeof(struct ve_hist_s)))
                    ret = -EFAULT;
            }
            break;
        }
		/**********************************************************************
		gamma ioctl
		**********************************************************************/
		case AMVECM_IOC_GAMMA_TABLE_EN: {
			vecm_latch_flag |= FLAG_GAMMA_TABLE_EN;
			break;
		}

		case AMVECM_IOC_GAMMA_TABLE_DIS: {
			vecm_latch_flag |= FLAG_GAMMA_TABLE_DIS;
			break;
		}

		case AMVECM_IOC_GAMMA_TABLE_R: {
			if (vecm_latch_flag & FLAG_GAMMA_TABLE_R)
			{
				ret = -EFAULT;
			}
			else if (copy_from_user(&video_gamma_table_r, (void __user *)arg, sizeof(struct tcon_gamma_table_s)))
			{
				ret = -EFAULT;
			}
			else
			{
				vecm_latch_flag |= FLAG_GAMMA_TABLE_R;
			}
			break;
		}

		case AMVECM_IOC_GAMMA_TABLE_G: {
			if (vecm_latch_flag & FLAG_GAMMA_TABLE_G)
			{
				ret = -EFAULT;
			}
			else if (copy_from_user(&video_gamma_table_g, (void __user *)arg, sizeof(struct tcon_gamma_table_s)))
			{
				ret = -EFAULT;
			}
			else
			{
				vecm_latch_flag |= FLAG_GAMMA_TABLE_G;
			}
			break;
		}

		case AMVECM_IOC_GAMMA_TABLE_B: {
			if (vecm_latch_flag & FLAG_GAMMA_TABLE_B)
			{
				ret = -EFAULT;
			}
			else if (copy_from_user(&video_gamma_table_b, (void __user *)arg, sizeof(struct tcon_gamma_table_s)))
			{
				ret = -EFAULT;
			}
			else
			{
				vecm_latch_flag |= FLAG_GAMMA_TABLE_B;
			}
			break;
		}

		case AMVECM_IOC_S_RGB_OGO: {
			if (vecm_latch_flag & FLAG_RGB_OGO)
			{
				ret = -EFAULT;
			}
			else if (copy_from_user(&video_rgb_ogo, (void __user *)arg, sizeof(struct tcon_rgb_ogo_s)))
			{
				ret = -EFAULT;
			}
			else
			{
				// en
				if (video_rgb_ogo.en > 1)
					video_rgb_ogo.en = 1;
				// r_pre_offset
				if (video_rgb_ogo.r_pre_offset > 1023)
					video_rgb_ogo.r_pre_offset = 1023;
				if (video_rgb_ogo.r_pre_offset < -1024)
					video_rgb_ogo.r_pre_offset = -1024;
				// g_pre_offset
				if (video_rgb_ogo.g_pre_offset > 1023)
					video_rgb_ogo.g_pre_offset = 1023;
				if (video_rgb_ogo.g_pre_offset < -1024)
					video_rgb_ogo.g_pre_offset = -1024;
				// b_pre_offset
				if (video_rgb_ogo.b_pre_offset > 1023)
					video_rgb_ogo.b_pre_offset = 1023;
				if (video_rgb_ogo.b_pre_offset < -1024)
					video_rgb_ogo.b_pre_offset = -1024;
				// r_gain
				if (video_rgb_ogo.r_gain > 2047)
					video_rgb_ogo.r_gain = 2047;
				// g_gain
				if (video_rgb_ogo.g_gain > 2047)
					video_rgb_ogo.g_gain = 2047;
				// b_gain
				if (video_rgb_ogo.b_gain > 2047)
					video_rgb_ogo.b_gain = 2047;
				// r_post_offset
				if (video_rgb_ogo.r_post_offset > 1023)
					video_rgb_ogo.r_post_offset = 1023;
				if (video_rgb_ogo.r_post_offset < -1024)
					video_rgb_ogo.r_post_offset = -1024;
				// g_post_offset
				if (video_rgb_ogo.g_post_offset > 1023)
					video_rgb_ogo.g_post_offset = 1023;
				if (video_rgb_ogo.g_post_offset < -1024)
					video_rgb_ogo.g_post_offset = -1024;
				// b_post_offset
				if (video_rgb_ogo.b_post_offset > 1023)
					video_rgb_ogo.b_post_offset = 1023;
				if (video_rgb_ogo.b_post_offset < -1024)
					video_rgb_ogo.b_post_offset = -1024;
				if (video_rgb_ogo_mode_sw)
					video_set_rgb_ogo();
				vecm_latch_flag |= FLAG_RGB_OGO;
			}
			break;
		}

		case AMVECM_IOC_G_RGB_OGO: {
			if (copy_to_user((void __user *)arg, &video_rgb_ogo, sizeof(struct tcon_rgb_ogo_s)))
			{
				ret = -EFAULT;
				break;
			}
			break;
		}

       default:
            return -EINVAL;
    }
    return ret;
}

static ssize_t amvecm_dnlp_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "0x%x\n", (am_ve_dnlp.en    << 28) |
                                  (am_ve_dnlp.rt    << 24) |
                                  (am_ve_dnlp.rl    << 16) |
                                  (am_ve_dnlp.black <<  8) |
                                  (am_ve_dnlp.white <<  0));
}
// [   28] en    0~1
// [27:20] rt    0~16
// [19:16] rl-1  0~15
// [15: 8] black 0~16
// [ 7: 0] white 0~16
static ssize_t amvecm_dnlp_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                      size_t count)
{
    size_t r;
    s32 val;

    r = sscanf(buf, "0x%x", &val);

    if ((r != 1) || (vecm_latch_flag & FLAG_VE_DNLP)){
        return -EINVAL;
    }

    am_ve_dnlp.en    = (val & 0xf0000000) >> 28;
    am_ve_dnlp.rt    =  (val & 0x0f000000) >> 24;
    am_ve_dnlp.rl    = (val & 0x00ff0000) >> 16;
    am_ve_dnlp.black =  (val & 0x0000ff00) >>  8;
    am_ve_dnlp.white = (val & 0x000000ff) >>  0;

    if (am_ve_dnlp.en    >  1)
        am_ve_dnlp.en    =  1;
    if (am_ve_dnlp.rl    > 64)
        am_ve_dnlp.rl    = 64;
    if (am_ve_dnlp.black > 16)
        am_ve_dnlp.black = 16;
    if (am_ve_dnlp.white > 16)
        am_ve_dnlp.white = 16;

    vecm_latch_flag |= FLAG_VE_DNLP;

    return count;
}

static ssize_t amvecm_brightness_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", vd1_brightness);
}

static ssize_t amvecm_brightness_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                      size_t count)
{
    size_t r;
    int val;

    r = sscanf(buf, "%d", &val);
    if ((r != 1) || (val < -1024) || (val > 1024)) {
        return -EINVAL;
    }

    vd1_brightness = val;
    vecm_latch_flag |= FLAG_BRI_CON;

    return count;
}

static ssize_t amvecm_contrast_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", vd1_contrast);
}

static ssize_t amvecm_contrast_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                    size_t count)
{
    size_t r;
    int val;

    r = sscanf(buf, "%d", &val);
    if ((r != 1) || (val < -1024) || (val > 1024)) {
        return -EINVAL;
    }

    vd1_contrast = val;
    vecm_latch_flag |= FLAG_BRI_CON;

    return count;
}
static int hue_pre = 0;  /*-25~25*/
static int saturation_pre = 0;  /*-128~127*/
static int hue_post = 0;  /*-25~25*/
static int saturation_post = 0;  /*-128~127*/
static ssize_t amvecm_saturation_hue_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return sprintf(buf, "0x%x\n", READ_MPEG_REG(VPP_VADJ1_MA_MB));
}

static ssize_t amvecm_saturation_hue_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                      size_t count)
{
    size_t r;
    s32 mab = 0;
    s16 mc = 0, md = 0;

    r = sscanf(buf, "0x%x", &mab);
    if ((r != 1) || (mab&0xfc00fc00)) {
        return -EINVAL;
    }

    WRITE_MPEG_REG(VPP_VADJ1_MA_MB, mab);
    mc = (s16)((mab<<22)>>22); // mc = -mb
    mc = 0 - mc;
    if (mc> 511)
        mc= 511;
    if (mc<-512)
        mc=-512;
    md = (s16)((mab<<6)>>22);  // md =  ma;
    mab = ((mc&0x3ff)<<16)|(md&0x3ff);
    WRITE_MPEG_REG(VPP_VADJ1_MC_MD, mab);
    WRITE_MPEG_REG(VPP_VADJ_CTRL, 1);
#ifdef PQ_DEBUG_EN
    printk("\n[amvideo..] set video_saturation_hue OK!!!\n");
#endif
    return count;
}
#include <linux/ctype.h>


static int parse_para_pq(const char *para, int para_num, int *result)
{
    char *endp;
    const char *startp = para;
    int *out = result;
    int len = 0, count = 0;

    if (!startp) {
        return 0;
    }

    len = strlen(startp);

    do {
        //filter space out
        while (startp && (isspace(*startp) || !isgraph(*startp)) && len) {
            startp++;
            len--;
        }

        if (len == 0) {
            break;
        }

        *out++ = simple_strtol(startp, &endp, 0);

        len -= endp - startp;
        startp = endp;
        count++;

    } while ((endp) && (count < para_num) && (len > 0));

    return count;
}

static ssize_t amvecm_saturation_hue_pre_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return snprintf(buf, 20, "%d %d\n", saturation_pre, hue_pre);
}

static ssize_t amvecm_saturation_hue_pre_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                      size_t count)
{
    int parsed[2];
    int i, ma, mb, mab, mc, md;
	int hue_cos[] = {
    256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245, /*0~12*/
    243, 241, 239, 237, 234, 231, 229, 226, 223, 220, 216, 213, 209  /*13~25*/
    };
    int hue_sin[] = {
    -147, -142, -137, -132, -126, -121, -115, -109, -104, -98, -92, -86, -80, /*-25~-13*/
     -74,  -68,  -62,  -56,  -50,  -44,  -38,  -31,  -25, -19, -13,  -6,      /*-12~-1*/
       0,								      /*0*/
       6,   13,   19,	25,   31,   38,   44,	50,   56,  62,	68,  74,      /*1~12*/
      80,   86,   92,	98,  104,  109,  115,  121,  126,  132, 137, 142, 147 /*13~25*/
    };

    if (likely(parse_para_pq(buf, 2, parsed) != 2)) {
	return -EINVAL;
    }

    if ((parsed[0] < -128) || (parsed[0] > 128) || (parsed[1] < -25) || (parsed[1] > 25)) {
	return -EINVAL;
    }
    saturation_pre = parsed[0];
    hue_pre = parsed[1];

    i = (hue_pre > 0) ? hue_pre: -hue_pre;
    ma = (hue_cos[i]*(saturation_pre + 128)) >> 7;
    mb = (hue_sin[25+hue_pre]*(saturation_pre + 128)) >> 7;

    if	(ma > 511) ma = 511;
    if (ma < -512) ma = -512;
	if (mb > 511)  mb = 511;
	if (mb < -512) mb = -512;
	mab =  ((ma & 0x3ff) << 16) | (mb & 0x3ff);

    printk("\n[amvideo..] saturation_pre:%d hue_pre:%d mab:%x\n", saturation_pre,hue_pre,mab);

    WRITE_MPEG_REG(VPP_VADJ2_MA_MB, mab);
    mc = (s16)((mab<<22)>>22); // mc = -mb
    mc = 0 - mc;
    if (mc > 511)  mc = 511;
    if (mc < -512) mc = -512;
    md = (s16)((mab<<6)>>22);  // md =	ma;
    mab = ((mc&0x3ff)<<16)|(md&0x3ff);
    WRITE_MPEG_REG(VPP_VADJ1_MC_MD, mab);
    WRITE_MPEG_REG_BITS(VPP_VADJ_CTRL, 1, 0, 1);

    return count;
}

static ssize_t amvecm_saturation_hue_post_show(struct class *cla, struct class_attribute *attr, char *buf)
{
    return snprintf(buf, 20, "%d %d\n", saturation_post, hue_post);
}

static ssize_t amvecm_saturation_hue_post_store(struct class *cla, struct class_attribute *attr, const char *buf,
                                      size_t count)
{
    int parsed[2];
    int i, ma, mb, mab, mc, md;
	int hue_cos[] = {
    256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245, /*0~12*/
    243, 241, 239, 237, 234, 231, 229, 226, 223, 220, 216, 213, 209  /*13~25*/
    };
    int hue_sin[] = {
    -147, -142, -137, -132, -126, -121, -115, -109, -104, -98, -92, -86, -80, /*-25~-13*/
     -74,  -68,  -62,  -56,  -50,  -44,  -38,  -31,  -25, -19, -13,  -6,      /*-12~-1*/
       0,								      /*0*/
       6,   13,   19,	25,   31,   38,   44,	50,   56,  62,	68,  74,      /*1~12*/
      80,   86,   92,	98,  104,  109,  115,  121,  126,  132, 137, 142, 147 /*13~25*/
    };

    if (likely(parse_para_pq(buf, 2, parsed) != 2)) {
	return -EINVAL;
    }

    if ((parsed[0] < -128) || (parsed[0] > 128) || (parsed[1] < -25) || (parsed[1] > 25)) {
	return -EINVAL;
    }
    saturation_post = parsed[0];
    hue_post = parsed[1];

    i = (hue_post > 0) ? hue_post: -hue_post;
    ma = (hue_cos[i]*(saturation_post + 128)) >> 7;
    mb = (hue_sin[25+hue_post]*(saturation_post + 128)) >> 7;

    if	(ma > 511) ma = 511;
    if (ma < -512) ma = -512;
	if (mb > 511)  mb = 511;
	if (mb < -512) mb = -512;
	mab =  ((ma & 0x3ff) << 16) | (mb & 0x3ff);

printk("\n[amvideo..] saturation_post:%d hue_post:%d mab:%x\n", saturation_post,hue_post,mab);

    WRITE_MPEG_REG(VPP_VADJ2_MA_MB, mab);
    mc = (s16)((mab<<22)>>22); // mc = -mb
    mc = 0 - mc;
    if (mc > 511)  mc = 511;
    if (mc < -512) mc = -512;
    md = (s16)((mab<<6)>>22);  // md =	ma;
    mab = ((mc&0x3ff)<<16)|(md&0x3ff);
    WRITE_MPEG_REG(VPP_VADJ2_MC_MD, mab);
    WRITE_MPEG_REG_BITS(VPP_VADJ_CTRL, 1, 2, 1);

    return count;
}


//static CLASS_ATTR(dnlp, S_IRUGO | S_IWUSR, amvecm_dnlp_show, amvecm_dnlp_store);
static struct class_attribute amvecm_class_attrs[] = {
	__ATTR(dnlp, S_IRUGO | S_IWUSR,
		amvecm_dnlp_show, amvecm_dnlp_store),
	__ATTR(brightness, S_IRUGO | S_IWUSR,
		amvecm_brightness_show, amvecm_brightness_store),
	__ATTR(contrast, S_IRUGO | S_IWUSR,
		amvecm_contrast_show, amvecm_contrast_store),
	__ATTR(saturation_hue,S_IRUGO | S_IWUSR,
		amvecm_saturation_hue_show,
		amvecm_saturation_hue_store),
	__ATTR(saturation_hue_pre,S_IRUGO | S_IWUSR,
		amvecm_saturation_hue_pre_show,
		amvecm_saturation_hue_pre_store),
	__ATTR(saturation_hue_post,S_IRUGO | S_IWUSR,
		amvecm_saturation_hue_post_show,
		amvecm_saturation_hue_post_store),
	__ATTR_NULL
};

#if 0
static int amvecm_probe(struct platform_device *pdev)
{
    int ret;

    ret = alloc_chrdev_region(&amvecm_devno, 0, AMVECM_COUNT, AMVECM_NAME);
	if (ret < 0) {
		pr_info("[amvecm.] : failed to allocate major number\n");
		return 0;
	}

    amvecm_clsp = class_create(THIS_MODULE, AMVECM_NAME);
    if (IS_ERR(amvecm_clsp))
    {
        pr_info(KERN_ERR "[amvecm.] : can't get amvecm_clsp\n");
        unregister_chrdev_region(amvecm_devno, 0);
        return PTR_ERR(amvecm_clsp);
	}

    /* allocate memory for the per-device structure */
    amvecm_devp = kmalloc(sizeof(struct amvecm_dev_s), GFP_KERNEL);
    if (!amvecm_devp)
    {
        pr_info("[amvecm.] : failed to allocate memory for amvecm device\n");
        return -ENOMEM;
    }

    memset(amvecm_devp, 0, sizeof(struct amvecm_dev_s));

    /* connect the file operations with cdev */
    cdev_init(&amvecm_devp->cdev, &amvecm_fops);
    amvecm_devp->cdev.owner = THIS_MODULE;
    ret = cdev_add(&amvecm_devp->cdev, amvecm_devno, 1);
    if (ret) {
        pr_err("[amvecm.] : failed to add device\n");
        /* @todo do with error */
        return ret;
    }
    /* create /dev nodes */
    //amvecm_devp->devt = MKDEV(MAJOR(amvecm_devno), 0);
    amvecm_devp->dev = device_create(amvecm_clsp, NULL, MKDEV(MAJOR(amvecm_devno), 0),
                        NULL, "%s%d", AMVECM_NAME, 0);
    if (IS_ERR(amvecm_devp->dev)) {
         pr_info("[amvecm.] : failed to create device node\n");
         cdev_del(&amvecm_devp->cdev);
		 kfree(amvecm_devp);
         return PTR_ERR(amvecm_devp->dev);;
    }

    device_create_file(amvecm_devp->dev, &dev_attr_dnlp);

    pr_info("[amvecm.] : driver probe ok\n");

    return ret;
}

static int amvecm_remove(struct platform_device *pdev)
{
    int i = 0;

    device_remove_file(amvecm_devp->dev, &dev_attr_dnlp);
    device_destroy(amvecm_clsp, MKDEV(MAJOR(amvecm_devno), i));
    cdev_del(&amvecm_devp->cdev);
    kfree(amvecm_devp);
    class_destroy(amvecm_clsp);
    unregister_chrdev_region(amvecm_devno, 0);

    pr_info("[amvecm.] : driver removed ok.\n");
    return 0;
}

#ifdef CONFIG_PM
static int amvecm_suspend(struct platform_device *pdev,pm_message_t state)
{
    pr_info("[amvecm.] : suspend module\n");
    return 0;
}

static int amvecm_resume(struct platform_device *pdev)
{
    pr_info("[amvecm.] : resume module\n");
    return 0;
}
#endif

static struct platform_driver amvecm_driver = {
    .probe      = amvecm_probe,
    .remove     = amvecm_remove,
#ifdef CONFIG_PM
    .suspend    = amvecm_suspend,
    .resume     = amvecm_resume,
#endif
    .driver     = {
        .name   = AMVECM_DRIVER_NAME,
    }
};
#endif
static struct file_operations amvecm_fops = {
    .owner   = THIS_MODULE,
    .open    = amvecm_open,
    .release = amvecm_release,
    .unlocked_ioctl   = amvecm_ioctl,
};

static int __init amvecm_init(void)
{
    //int ret = 0;

    //ret = platform_driver_register(&amvecm_driver);
    //if (ret != 0) {
    //    pr_info("failed to register amvecm module, error %d\n", ret);
    //    return -ENODEV;
    //}

    int ret = 0;
	int i = 0;
    struct amvecm_dev_s *devp = &amvecm_dev;

    memset(devp, 0, (sizeof(struct amvecm_dev_s)));
	printk("\n\n VECM init \n\n");
    ret = alloc_chrdev_region(&devp->devno, 0, 1, AMVECM_NAME);
    if (ret < 0)
    {
        goto fail_alloc_region;
    }

    devp->clsp = class_create(THIS_MODULE, AMVECM_CLASS_NAME);
    if (IS_ERR(devp->clsp)) {
        ret = PTR_ERR(devp->clsp);
        goto fail_create_class;
    }

	for(i = 0; amvecm_class_attrs[i].attr.name; i++){
		if(class_create_file(devp->clsp,
				&amvecm_class_attrs[i]) < 0)
		goto fail_class_create_file;
	}

    cdev_init(&devp->cdev, &amvecm_fops);
    devp->cdev.owner = THIS_MODULE;
    ret = cdev_add(&devp->cdev, devp->devno, 1);
    if (ret)
    {
        goto fail_add_cdev;
    }

    devp->dev = device_create(devp->clsp, NULL, devp->devno, NULL, AMVECM_NAME);

    if (IS_ERR(devp->dev)) {
        ret = PTR_ERR(devp->dev);
        goto fail_create_device;
    }

    return 0;

fail_create_device:
	pr_info("[amvecm.] : amvecm device create error.\n");
    cdev_del(&devp->cdev);
fail_add_cdev:
	pr_info("[amvecm.] : amvecm add device error.\n");
    kfree(devp);
fail_class_create_file:
	pr_info("[amvecm.] : amvecm class create file error.\n");
	for(i=0; amvecm_class_attrs[i].attr.name; i++){
		class_remove_file(devp->clsp,
				&amvecm_class_attrs[i]);
	}
    class_destroy(devp->clsp);
fail_create_class:
	pr_info("[amvecm.] : amvecm class create error.\n");
    unregister_chrdev_region(devp->devno, 1);
fail_alloc_region:
    pr_info("[amvecm.] : amvecm alloc error.\n");
    pr_info("[amvecm.] : amvecm_init.\n");
    return ret;
}

static void __exit amvecm_exit(void)
{
    //platform_driver_unregister(&amvecm_driver);

    struct amvecm_dev_s *devp = &amvecm_dev;

    device_destroy(devp->clsp, devp->devno);
    cdev_del(&devp->cdev);
    class_destroy(devp->clsp);
    unregister_chrdev_region(devp->devno, 1);
    kfree(devp);

    pr_info("[amvecm.] : amvecm_exit.\n");
}

module_init(amvecm_init);
module_exit(amvecm_exit);

MODULE_DESCRIPTION("AMLOGIC amvecm driver");
MODULE_LICENSE("GPL");

