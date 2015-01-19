#ifndef _DI_MODULE_H
#define _DI_MODULE_H
#define DI_COUNT   1
/*****************************
*    di attr management
******************************/

/************************************
*    di device structure
*************************************/
typedef struct deinterlace_dev_s{

    dev_t                       devt;
    struct cdev cdev;             /* The cdev structure */
    struct device               *dev;

    struct task_struct *task;

    unsigned char di_event;
}di_dev_t;

#define DI_VER "2011Jan25"
#endif
