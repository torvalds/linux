
#ifndef _AW_CLOCK_H_
#define _AW_CLOCK_H_





struct clk{

    s16 clk_id;       // clock id ,  get at the initial
    s16 parent_id;    // source id , get at the initial

    u8 usrcnt;       // source clock counter, counter the num of children clock tree
    u8 used;				// reference, resource lock.
    s16 onoff; // mod clock ,gate on or off 1: on, 0: off    ; source clock

    struct clk *parent;//get at the initial,if no parent,set NULL.
    const char *name;// name of moudle,get at the intial

    u32 freq;  // src clock, get at the initial
    u16 div;   // module clock, divide ratio, get at the initial
    u16 div_max; // module clock divider max


    int  (*enable)(struct clk *clk);  // clock on,this  function must be called after clk_get
    void (*disable)(struct clk *clk); // clock off,when module quit or for power managment
    int  (*set_parent)(struct clk *parent, struct clk *clk); // it will disable clock,change,then enable clock.
    struct clk* (*get_parent)(struct clk *clk); // get the parent of clock,NULL means no parent.
    int (*set_rate)(struct clk *clk, unsigned long rate);//sys clk: frequency; mod clk: divider.
    unsigned long (*get_rate)(struct clk *clk);   // sys clk: frequency; mod clk: divider.
    int (*mod_reset)(struct clk *clk, int reset); // for image0-1, scale0-1, usb0-2, etc...
  //  int (*set_bias_current)(struct clk *clk, unsigned int bias); // for future

    struct list_head node; /* parent list, temporary not used */

};


#endif

