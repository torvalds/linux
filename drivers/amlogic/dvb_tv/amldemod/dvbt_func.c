#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "aml_demod.h"
#include "demod_func.h"

static int debug_amldvbt;
module_param(debug_amldvbt, int, 0644);
MODULE_PARM_DESC(debug_amldvbt, "turn on debugging (default: 0)");
#define dprintk(args...) do { if (debug_amldvbt) { printk(KERN_DEBUG "AMLDVBT: "); printk(args); printk("\n"); } } while (0)

static int tuner_type = 3;

static void set_ACF_coef(int ADsample, int bandwidth)
{
    if (ADsample == 45)	{
	// Set ACF and IIREQ
	if (bandwidth == 0) {//8M Hz
	    apb_write_reg(2, 0x2c ,0x255);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x0B5);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x091);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x02E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x253);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x0CB);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x2CD);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x07C);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x250);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0E4);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x276);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05D);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x24D);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0F3);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x25E);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x24A);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0FD);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x256);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003effff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003cefbe);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003adf7c);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x0038bf39);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x003696f5);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x003466b0);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x00322e69);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x002fee21);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x002dadd9);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002b6d91);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x00291d48);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x0026ccfe);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x00245cb2);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x0021d463);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x001f2410);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x001c3bb6);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x00192b57);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x0015e2f1);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x00127285);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x000eca14);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x000ac99b);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x00063913);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x0000c073);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x003a3fb4);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x00347ecf);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x002ff649);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x002a8dab);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x002444f0);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x001d0c1b);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x000fc300);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x000118ce);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x003c17c3);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x00000751);
	}
	else if (bandwidth==1) {// 7Mhz

	    apb_write_reg(2, 0x2c ,0x24B);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x0BD);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x04B);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x03E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x246);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x0D1);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x2A2);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x07C);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x241);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0E7);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x25B);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05D);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x23D);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0F5);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x248);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x23A);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0FD);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x242);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN
	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f07ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003cffbf);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003aef7e);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x0038d73c);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x0036b6f9);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x003486b3);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x00324e6d);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x00300e25);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x002dcddd);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002b8594);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x00292d4b);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x0026d500);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x00245cb3);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x0021cc62);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x001f0c0d);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x001c1bb3);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x0018fb52);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x0015b2eb);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x00123a7f);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x000e9a0e);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x000a9995);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x0006090d);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x0000a06e);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x003a57b3);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x0034ded8);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x00309659);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x002b75c4);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x0025350e);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x001dec37);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x00126b28);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x00031130);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x003cffec);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x00000767);
	}
	else if (bandwidth==2) {// 6MHz
	    apb_write_reg(2, 0x2c ,0x240);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x0C6);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x3F9);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x03E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x23A);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x0D7);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x27B);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x07C);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x233);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0EA);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x244);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05D);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x22F);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0F6);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x235);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x22B);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0FD);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x231);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN
	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f07ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003cffbf);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003aef7e);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x0038d73c);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x0036b6f8);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x003486b3);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x0032466c);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x002ffe24);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x002dadda);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002b5d90);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x0028fd45);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002694f9);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x002414ab);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x00217458);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x001ea402);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x001ba3a5);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x00187342);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x00151ad9);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x0011926b);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x000dc9f6);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x0009a178);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x0004d8eb);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x003f4045);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x0038e785);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x00337eab);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x002f3e2d);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x002a1599);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x0023ace1);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x001b33fb);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x000cd29c);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x0001c0c1);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x003cefde);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x0000076a);
	}
	else { // 5MHz
	    apb_write_reg(2, 0x2c ,0x236);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x0CE);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x39A);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x03E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x22F);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x0DE);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x257);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x07C);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x227);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0EE);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x230);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05D);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x222);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0F8);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x225);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x21E);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0FE);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x222);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN
	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003effff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003ce7bd);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003ac77a);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x0038a737);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x00367ef2);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x00344eac);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x00321e66);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x002fee20);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x002dbdda);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002b8d94);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x00295d4e);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x00272508);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x0024dcc0);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x00227475);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x001fe426);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x001d1bd1);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x001a2374);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x0016e311);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x00136aa6);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x000fba33);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x000ba9b8);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x0007092e);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x0001988e);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x003b37d0);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x0035aef3);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x00316673);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x002c45de);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x0025e527);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x001da444);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x000deaea);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x000178bf);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x003cb7d6);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x00000765);
	}
    }
    else if (ADsample == 28) {
	// 28.5714 MHz Set ACF
	if (bandwidth == 0) {  //8M Hz

	    apb_write_reg(2, 0x2c ,0x2DB);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x05B);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x163);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x00E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x2D5);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x08B);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x3BC);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x06D);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x2CF);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0BF);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x321);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x008);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x2C9);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0E3);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x2EE);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x058);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x2C3);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0F8);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x2DD);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04D);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003ef7ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d37c0);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003c3f94);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x003b0f78);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x0038c73f);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x00369ef1);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x003576be);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x0033b698);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x0031164d);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002f1dfd);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002de5cf);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002c15a2);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x0029f560);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x0027bd1b);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x00252ccf);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x0022bc7c);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x00207c34);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001da3e5);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001a9b83);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x0017db27);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x001432c6);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x000fa23e);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000b91af);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x00077136);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x0002c090);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003ec01a);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x003a3f92);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x00354efa);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x002fee54);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x002a35a3);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x0023f4e4);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x001cdc12);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x00000316);
	}
	else if (bandwidth==1) {
	    apb_write_reg(2, 0x2c ,0x2C2);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x069);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x134);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x00E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x2B7);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x095);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x36F);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x06D);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x2AA);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0C6);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x2E5);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x008);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x2A1);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0E6);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x2BA);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x058);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x299);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0F9);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x2AC);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04D);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003ee7ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d1fbc);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003bf790);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x003a876a);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x00388f31);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x0036c6f3);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x003536bf);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x00334689);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x00310644);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002ef5fd);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002d45c2);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002b7d8c);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x00298550);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x00278510);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x00252ccc);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x0022847c);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x00201427);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001e03e0);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001b6b9b);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x0017c336);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x0013e2b8);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x0010b246);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000d81e8);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x00084966);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x0003089c);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003f0022);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x003aaf9c);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x00360f0c);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x00312e74);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x002c05d3);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x00268d2a);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x0020bc76);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x000003b3);
	}
	else if (bandwidth==2) {// 6MHz

	    apb_write_reg(2, 0x2c ,0x2A9);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x078);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x0F4);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x01E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x299);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x0A1);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x321);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x078);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x288);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0CD);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x2AE);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05F);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x27C);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0E9);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x28B);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x058);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x273);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0FA);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x281);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04D);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f17ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d3fc4);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003b7f8a);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x0039df55);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x00381720);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x00360ee2);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x00342ea1);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x0032ee6e);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x0031e64e);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x00300e22);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002daddc);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002b758f);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x0029ad51);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x0027ad18);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x00250ccd);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x00227476);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x00204c2a);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001de3e6);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001a838a);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x0016ab12);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x00137a9d);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x00113a4a);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000db1f8);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x0007c15f);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x00022883);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003df803);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x00398f79);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x0034d6e6);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x002fd64b);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x002a8da7);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x002504fa);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x001f2443);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x00000382);
	}
	else {
	    apb_write_reg(2, 0x2c ,0x28F);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x088);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x09E);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x01E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x27C);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x0AD);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x2D6);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x078);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x268);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0D4);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x27C);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05F);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x25B);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0ED);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x262);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x058);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x252);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0FB);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x25A);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04D);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f17ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d4fc5);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003baf8e);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x003a3f5d);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x0038df32);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x00374703);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x00354ec9);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x00333e88);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x00314e47);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002f860c);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002d9dd2);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002b5590);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x0028cd42);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x00266cf2);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x00245cab);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x00225c6b);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x00200427);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001d4bd5);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001a9b7d);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x00183b2b);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x0015b2e1);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x00122a83);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000d49fc);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x0007594e);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x00024080);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003e980e);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x003ab796);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x00368f15);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x00320e8a);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x002d25f4);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x0027ad4f);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x00219496);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x000003c9);
	}
    }
    else {
	// 20.7 MHz Set ACF
	if (bandwidth == 0) {   //8M Hz

	    apb_write_reg(2, 0x2c ,0x318);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x03E);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x1AE);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x00E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x326);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x074);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x074);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x06F);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x336);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0B1);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x3C9);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x008);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x33F);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0DC);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x384);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x340);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0F6);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x36D);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f37ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d97cc);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003bf798);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x003a4f64);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x0038a72f);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x0036f6f9);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x003546c3);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x0033868c);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x0031be54);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002fe61a);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002e05df);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002c15a2);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x002a1562);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x0027f520);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x0025c4dc);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x00236c93);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x0020f446);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001e4bf4);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001b739d);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x00185b3d);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x0014ead5);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x00111a62);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000cb9df);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x00079148);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x00030093);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003f802a);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x003b77b2);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x0036a725);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x0030ae7b);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x00285d9f);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x001abc46);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x000f8a85);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x00000187);
	}
	else if (bandwidth==1) {
	    apb_write_reg(2, 0x2c ,0x2F9);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x04C);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x18E);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x00E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x2FD);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x07F);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x01A);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x06D);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x300);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0B8);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x372);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05F);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x301);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0DF);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x335);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x2FE);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0F7);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x320);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f37ff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d8fcc);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003bef97);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x003a4762);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x0038972d);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x0036e6f7);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x00352ec1);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x00336e89);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x00319e50);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002fce16);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002de5db);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002bf59d);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x0029ed5e);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x0027d51c);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x00259cd7);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x0023448e);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x0020cc41);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001e23ef);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001b4b98);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x00183339);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x0014cad1);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x0010fa5e);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000c99dc);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x00078145);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x0002f892);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003f802a);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x003b8fb3);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x0036d729);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x00310682);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x00290dae);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x001c0c67);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x0010a2ad);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x000001a8);
	}
	else if (bandwidth==2) {// 6MHz
	    apb_write_reg(2, 0x2c ,0x2D9);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x05C);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x161);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x00E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x2D4);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x08B);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x3B8);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x06B);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x2CD);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0C0);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x31E);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05F);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x2C7);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0E3);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x2EB);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x2C1);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0F8);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x2DA);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN
	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f2fff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d87cb);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003bdf96);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x003a2f60);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x00387f2a);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x0036c6f4);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x00350ebd);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x00334684);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x0031764b);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002f9e11);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002db5d4);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002bbd97);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x0029b557);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x00279515);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x00255ccf);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x00230c87);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x0020943a);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001debe8);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001b1b91);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x00180b33);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x0014aacc);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x0010e25a);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000c91da);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x00078945);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x00031895);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003fa82e);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x003bbfb8);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x00371730);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x0031668c);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x00299dbc);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x001d1480);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x00119acf);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x000001c4);
	}
	else {
	    apb_write_reg(2, 0x2c ,0x2B9);// ACF_STAGE1_A1
	    apb_write_reg(2, 0x2d ,0x06E);// ACF_STAGE1_A2
	    apb_write_reg(2, 0x2e ,0x11E);// ACF_STAGE1_B1
	    apb_write_reg(2, 0x2f ,0x01E);// ACF_STAGE1_GAIN
	    apb_write_reg(2, 0x30 ,0x2AB);// ACF_STAGE2_A1
	    apb_write_reg(2, 0x31 ,0x099);// ACF_STAGE2_A2
	    apb_write_reg(2, 0x32 ,0x351);// ACF_STAGE2_B1
	    apb_write_reg(2, 0x33 ,0x06B);// ACF_STAGE2_GAIN
	    apb_write_reg(2, 0x34 ,0x29D);// ACF_STAGE3_A1
	    apb_write_reg(2, 0x35 ,0x0C8);// ACF_STAGE3_A2
	    apb_write_reg(2, 0x36 ,0x2D0);// ACF_STAGE3_B1
	    apb_write_reg(2, 0x37 ,0x05F);// ACF_STAGE3_GAIN
	    apb_write_reg(2, 0x38 ,0x292);// ACF_STAGE4_A1
	    apb_write_reg(2, 0x39 ,0x0E7);// ACF_STAGE4_A2
	    apb_write_reg(2, 0x3a ,0x2A8);// ACF_STAGE4_B1
	    apb_write_reg(2, 0x3b ,0x05A);// ACF_STAGE4_GAIN
	    apb_write_reg(2, 0x3c ,0x28A);// ACF_STAGE5_A1
	    apb_write_reg(2, 0x3d ,0x0F9);// ACF_STAGE5_A2
	    apb_write_reg(2, 0x3e ,0x29B);// ACF_STAGE5_B1
	    apb_write_reg(2, 0x3f ,0x04B);// ACF_STAGE5_GAIN

	    apb_write_reg(2, 0xfe, 0x000);apb_write_reg(2, 0xff, 0x003f2fff);
	    apb_write_reg(2, 0xfe, 0x001);apb_write_reg(2, 0xff, 0x003d7fca);
	    apb_write_reg(2, 0xfe, 0x002);apb_write_reg(2, 0xff, 0x003bcf94);
	    apb_write_reg(2, 0xfe, 0x003);apb_write_reg(2, 0xff, 0x003a1f5e);
	    apb_write_reg(2, 0xfe, 0x004);apb_write_reg(2, 0xff, 0x00386727);
	    apb_write_reg(2, 0xfe, 0x005);apb_write_reg(2, 0xff, 0x0036a6f0);
	    apb_write_reg(2, 0xfe, 0x006);apb_write_reg(2, 0xff, 0x0034e6b8);
	    apb_write_reg(2, 0xfe, 0x007);apb_write_reg(2, 0xff, 0x0033167f);
	    apb_write_reg(2, 0xfe, 0x008);apb_write_reg(2, 0xff, 0x00314645);
	    apb_write_reg(2, 0xfe, 0x009);apb_write_reg(2, 0xff, 0x002f660a);
	    apb_write_reg(2, 0xfe, 0x00a);apb_write_reg(2, 0xff, 0x002d75cd);
	    apb_write_reg(2, 0xfe, 0x00b);apb_write_reg(2, 0xff, 0x002b758e);
	    apb_write_reg(2, 0xfe, 0x00c);apb_write_reg(2, 0xff, 0x0029654e);
	    apb_write_reg(2, 0xfe, 0x00d);apb_write_reg(2, 0xff, 0x0027450a);
	    apb_write_reg(2, 0xfe, 0x00e);apb_write_reg(2, 0xff, 0x002504c4);
	    apb_write_reg(2, 0xfe, 0x00f);apb_write_reg(2, 0xff, 0x0022a47b);
	    apb_write_reg(2, 0xfe, 0x010);apb_write_reg(2, 0xff, 0x0020242d);
	    apb_write_reg(2, 0xfe, 0x011);apb_write_reg(2, 0xff, 0x001d7bdb);
	    apb_write_reg(2, 0xfe, 0x012);apb_write_reg(2, 0xff, 0x001aa383);
	    apb_write_reg(2, 0xfe, 0x013);apb_write_reg(2, 0xff, 0x00178b24);
	    apb_write_reg(2, 0xfe, 0x014);apb_write_reg(2, 0xff, 0x00142abd);
	    apb_write_reg(2, 0xfe, 0x015);apb_write_reg(2, 0xff, 0x0010624a);
	    apb_write_reg(2, 0xfe, 0x016);apb_write_reg(2, 0xff, 0x000c11ca);
	    apb_write_reg(2, 0xfe, 0x017);apb_write_reg(2, 0xff, 0x00070935);
	    apb_write_reg(2, 0xfe, 0x018);apb_write_reg(2, 0xff, 0x00029885);
	    apb_write_reg(2, 0xfe, 0x019);apb_write_reg(2, 0xff, 0x003f281e);
	    apb_write_reg(2, 0xfe, 0x01a);apb_write_reg(2, 0xff, 0x003b3fa9);
	    apb_write_reg(2, 0xfe, 0x01b);apb_write_reg(2, 0xff, 0x00369720);
	    apb_write_reg(2, 0xfe, 0x01c);apb_write_reg(2, 0xff, 0x0030ce7b);
	    apb_write_reg(2, 0xfe, 0x01d);apb_write_reg(2, 0xff, 0x0028dda7);
	    apb_write_reg(2, 0xfe, 0x01e);apb_write_reg(2, 0xff, 0x001c6464);
	    apb_write_reg(2, 0xfe, 0x01f);apb_write_reg(2, 0xff, 0x0011b2c7);
	    apb_write_reg(2, 0xfe, 0x020);apb_write_reg(2, 0xff, 0x000001cb);
	}
    }
}

static void dvbt_reg_initial(struct aml_demod_sta *demod_sta)
{
    u32 clk_freq;
    u32 adc_freq;
    u8  ch_mode;
    u8  agc_mode;
    u32 ch_freq;
    u16 ch_if;
    u16 ch_bw;
    u16 symb_rate;

    u8  bw;
    u8  sr;
    u8  ifreq;
    u32 tmp;

    clk_freq  = demod_sta->clk_freq ; // kHz
    adc_freq  = demod_sta->adc_freq ; // kHz
    ch_mode   = demod_sta->ch_mode  ;
    agc_mode  = demod_sta->agc_mode ;
    ch_freq   = demod_sta->ch_freq  ; // kHz
    ch_if     = demod_sta->ch_if    ; // kHz
    ch_bw     = demod_sta->ch_bw    ; // kHz
    symb_rate = demod_sta->symb_rate; // k/sec

    bw = 8 - ch_bw/1000;
    sr = adc_freq > 40000 ? 3 :	adc_freq > 24000 ? 2 :
	adc_freq > 20770 ? 1 : 0;
    ifreq = ch_if > 35000 ? 0 : 1;

    //////////////////////////////////////
    // bw == 0 : 8M
    //       1 : 7M
    //       2 : 6M
    //       3 : 5M
    // sr == 0 : 20.7M
    //       1 : 20.8333M
    //       2 : 28.5714M
    //       3 : 45M
    // ifreq == 0: 36.13MHz
    //          1: 4.57MHz
    // agc_mode == 0: single AGC
    //             1: dual AGC
    //////////////////////////////////////
    apb_write_reg(2, 0x02, 0x00800000);  // SW reset bit[23] ; write anything to zero
    apb_write_reg(2, 0x00, 0x00000000);

    switch (sr) {
    case 0: apb_write_reg(2, 0x08, 0x00002966);break;
    case 1: apb_write_reg(2, 0x08, 0x00002999);break;
    case 2: apb_write_reg(2, 0x08, 0x00003924);break;
    case 3: apb_write_reg(2, 0x08, 0x00005a00);break;  // sample_rate	//45M
    default : break;
    }

    apb_write_reg(2, 0x0d, 0x00000000);
    apb_write_reg(2, 0x0e, 0x00000000);
    dvbt_enable_irq( 8 );

    apb_write_reg(2, 0x11, 0x00100002);   // FSM [15:0] TIMER_FEC_LOST
    apb_write_reg(2, 0x12, 0x02100201);   // FSM
    apb_write_reg(2, 0x14, 0xe81c4ff6);   // AGC_TARGET 0xf0121385
    apb_write_reg(2, 0x15, 0x02050ca6);   // AGC_CTRL

    switch (sr) {
    case 0: apb_write_reg(2, 0x15, apb_read_reg(2, 0x15) | (0x5b << 12));break;
    case 1: apb_write_reg(2, 0x15, apb_read_reg(2, 0x15) | (0x5b << 12));break;
    case 2: apb_write_reg(2, 0x15, apb_read_reg(2, 0x15) | (0x7b << 12));break;
    case 3: apb_write_reg(2, 0x15, apb_read_reg(2, 0x15) | (0xc2 << 12));break;  // sample_rate	//45M
    default : break;
    }

    if (agc_mode ==0) apb_write_reg(2, 0x16, 0x67f80);  // AGC_IFGAIN_CTRL
    else if (agc_mode ==1) apb_write_reg(2, 0x16, 0x07f80);  // AGC_IFGAIN_CTRL

    apb_write_reg(2, 0x17, 0x07f80);  // AGC_RFGAIN_CTRL
    apb_write_reg(2, 0x18, 0x00000000);  // AGC_IFGAIN_ACCUM
    apb_write_reg(2, 0x19, 0x00000000);  // AGC_RFGAIN_ACCUM

    /*
    if (ifreq == 0){
    	switch (sr){
	case 0: apb_write_reg(2, 0x20, 0x00002096);break;  // DDC NORM_PHASE    36.13M IF  For 20.7M sample rate
        case 1: apb_write_reg(2, 0x20, 0x000021a9);break;  // DDC NORM_PHASE    36.13M IF  For 20.8333M sample rate
        case 2: apb_write_reg(2, 0x20, 0x000021dc);break;  // DDC NORM_PHASE    36.13M IF  For 28.57142M sample rate
        case 3: apb_write_reg(2, 0x20, 0x000066e2);break;  // DDC NORM_PHASE    36.13M IF  For 45M sample rate
        default :break;
	}
    }
    else if (ifreq == 1){
    	switch (sr){
	case 0: apb_write_reg(2, 0x20, 0x00001c42);break;  // DDC NORM_PHASE    4.57M IF  For 20.7M sample rate
        case 1: apb_write_reg(2, 0x20, 0x00001c1f);break;  // DDC NORM_PHASE    4.57M IF  For 20.8333M sample rate
        case 2: apb_write_reg(2, 0x20, 0x00001479);break;  // DDC NORM_PHASE    4.57M IF  For 28.57142M sample rate
        case 3: apb_write_reg(2, 0x20, 0x0000d00);break;  // DDC NORM_PHASE    4.57M IF  For 45M sample rate
        default : break;
	}
    }
    */

    tmp = ch_if * (1<<15) / adc_freq;
    tmp &= 0x3fff;
    apb_write_reg(2, 0x20, tmp);
    if (demod_sta->debug)
	printk("IF: %d kHz  ADC: %d kHz  DDC: %04x\n", ch_if, adc_freq, tmp);

    apb_write_reg(2, 0x21, 0x001ff000);  // DDC CS_FCFO_ADJ_CTRL
    apb_write_reg(2, 0x22, 0x00000000);  // DDC ICFO_ADJ_CTRL
    apb_write_reg(2, 0x23, 0x00004000);  // DDC TRACK_FCFO_ADJ_CTRL
    apb_write_reg(2, 0x27, 0x00a98200);
    // [23] agc state mode [22:19] icfo_time_limit ;[18:15] tps_time_limit ;[14:4] cs_cfo_thres ;  [3:0] fsm_state_d;
    //            1              010,1                   001,1                000,0010,0000,                xxxx
    apb_write_reg(2, 0x28, 0x04028032);
    // [31:24] cs_Q_thres; [23:13] sfo_thres;  FSM [12:0] fcfo_thres;;
    //      0000,0100,        0000,0010,100          0,0000,0011,0010
    apb_write_reg(2, 0x29, 0x0051117F);
    //apb_write_reg(2, 0x29, 0x00010f7F);
    //  [18:16] fec_rs_sh_ctrl ;[15:9] fsm_total_timer;[8:6] modeDet_time_limit; FSM [5:0] sfo_time_limit; ;
    //        01,                ()   0000,111             1,01                     11,1111

    // SRC NORM_INRATE
    switch(bw){
    case 0: tmp = (1 << 14) * adc_freq / 125 / 8 * 7;break;
    case 1: tmp = (1 << 14) * adc_freq / 125;break;
    case 2: tmp = (1 << 14) * adc_freq / 125 / 6 * 7;break;
    case 3: tmp = (1 << 14) * adc_freq / 125 / 5 * 7;break;
    default:	tmp = (1 << 14) * adc_freq / 125 / 8 * 7;break;
    }
    apb_write_reg(2, 0x44, tmp&0x7fffff);

    apb_write_reg(2, 0x45, 0x00000000);  // SRC SRC_PHASE_INI
    apb_write_reg(2, 0x46, 0x02004000);  // SRC SFO_ADJ_CTRL SFO limit 0x100!!
    apb_write_reg(2, 0x48, 0xc0287);     // DAGC_CTRL
    apb_write_reg(2, 0x49, 0x00000005);  // DAGC_CTRL1
    apb_write_reg(2, 0x4c, 0x00000bbf);  // CCI_RP
    apb_write_reg(2, 0x4d, 0x00000376);  // CCI_RPSQ
    apb_write_reg(2, 0x4e, 0x00202109);  // CCI_CTRL
    apb_write_reg(2, 0x52, 0x00000000);  // CCI_NOTCH1_A2
    apb_write_reg(2, 0x53, 0x00000000);  // CCI_NOTCH1_B1
    apb_write_reg(2, 0x54, 0x00c00000);  // CCI_NOTCH2_A1
    apb_write_reg(2, 0x55, 0x00000000);  // CCI_NOTCH2_A2
    apb_write_reg(2, 0x56, 0x00000000);  // CCI_NOTCH2_B1
    apb_write_reg(2, 0x57, 0x00000000);  // CCI_NOTCH2_B1
    apb_write_reg(2, 0x58, 0x00000886);  // MODE_DETECT_CTRL
    apb_write_reg(2, 0x5c, 0x00001011);  // ICFO_EST_CTRL
    apb_write_reg(2, 0x5f, 0x00010503);  // TPS_FCFO_CTRL
    apb_write_reg(2, 0x61, 0x00000003);  // DE_PN_CTRL
    apb_write_reg(2, 0x61, apb_read_reg(2, 0x61) | (1<<2));  // DE_PN_CTRL SP sync close , Use TPS only ;
    apb_write_reg(2, 0x68, 0x004060c0);  // CHAN_EST_CTRL0
    apb_write_reg(2, 0x68, apb_read_reg(2, 0x68) &~(1<<7));  // SNR report filter;
    //apb_write_reg(2, 0x68, apb_read_reg(2, 0x68) &~(1<<13));  // Timing Adjust Shutdown;
    apb_write_reg(2, 0x69, 0x148c3812);  // CHAN_EST_CTRL1
    //apb_write_reg(2, 0x69, apb_read_reg(2, 0x69) | (1<<10));  // Disable FD data update
    //apb_write_reg(2, 0x69, apb_read_reg(2, 0x69) | (1<<9));  // set FD coeff
    //apb_write_reg(2, 0x69, apb_read_reg(2, 0x69) | (1<<8));  // set TD coeff
    apb_write_reg(2, 0x6a, 0x9101012d);  // CHAN_EST_CTRL2
    apb_write_reg(2, 0x6b, 0x00442211);  // CHAN_EST_CTRL2
    apb_write_reg(2, 0x6c, 0x01fc040a);  // CHAN_EST_CTRL3
    apb_write_reg(2, 0x6d, 0x0030303f);  // SET SNR THRESHOLD
    apb_write_reg(2, 0x73, 0xffffffff);  // CCI0_PILOT_UPDATE_CTRL
    apb_write_reg(2, 0x74, 0xffffffff);  // CCI0_DATA_UPDATE_CTRL
    apb_write_reg(2, 0x75, 0xffffffff);  // CCI1_PILOT_UPDATE_CTRL
    apb_write_reg(2, 0x76, 0xffffffff);  // CCI1_DATA_UPDATE_CTRL

    // Set ACF and ACFEQ coeffecient
    switch(sr){
    case 0: set_ACF_coef(21, bw);break;
    case 1: set_ACF_coef(21, bw);break;
    case 2: set_ACF_coef(28, bw);break;
    case 3: set_ACF_coef(45, bw);break;
    default : break;
    }
    apb_write_reg(2, 0x78, 0x000001a2);  // FEC_CTRL  parallel mode ; [27:24] is TS clk/valid/sync/error
    apb_write_reg(2, 0x7d, 0x0000009d);
    apb_write_reg(2, 0xd6, 0x00000003);
    apb_write_reg(2, 0xd7, 0x00000008);
    apb_write_reg(2, 0xd8, 0x00000120);
    apb_write_reg(2, 0xd9, 0x01010101);
    apb_write_reg(2, 0x04, 0x00000000);  // TPS Current, QPSK, none Hierarchy, HP, LP 1/2

    tmp = (1<<25) | ((bw&3)<<20) | (1<<16) | (1<<1);
    apb_write_reg(2, 0x02, tmp);
    apb_write_reg(2, 0x03, (1<<6)); // Cordic parameter Calc

    udelay(1);

    tmp = apb_read_reg(2, 0x02);
    tmp |= (1<<24) | 1; // FSM, Demod enable.
    apb_write_reg(2, 0x02, tmp);
}


int dvbt_set_ch(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_dvbt *demod_dvbt)
{
    int ret = 0;
    u8 bw, sr, ifreq, agc_mode;
    u32 ch_freq;

    bw       = demod_dvbt->bw;
    sr       = demod_dvbt->sr;
    ifreq    = demod_dvbt->ifreq;
    agc_mode = demod_dvbt->agc_mode;
    ch_freq  = demod_dvbt->ch_freq;

    // Set registers
    //////////////////////////////////////
    // bw == 0 : 8M
    //       1 : 7M
    //       2 : 6M
    //       3 : 5M
    // sr == 0 : 20.7M
    //       1 : 20.8333M
    //       2 : 28.5714M
    //       3 : 45M
    // ifreq == 0: 36.13MHz
    //          1: 4.57MHz
    // agc_mode == 0: single AGC
    //             1: dual AGC
    //////////////////////////////////////
    if (bw > 3) {
    	printk("Error: Invalid Bandwidth option %d\n", bw);
	bw = 0;
	ret = -1;
    }

    if (sr > 3) {
    	printk("Error: Invalid Sampling Freq option %d\n", sr);
	sr = 2;
	ret = -1;
    }

    if (ifreq > 1) {
    	printk("Error: Invalid IFreq option %d\n", ifreq);
	ifreq = 0;
	ret = -1;
    }

    if (agc_mode > 3) {
    	printk("Error: Invalid AGC mode option %d\n", agc_mode);
	agc_mode = 0;
	ret = -1;
    }

    // if (ret != 0) return ret;

    // Set DVB-T
    (*DEMOD_REG0) |= 1;

    demod_sta->dvb_mode  = 1;
    demod_sta->ch_mode   = 0; // TODO
    demod_sta->agc_mode  = agc_mode;
    demod_sta->ch_freq   = ch_freq;
    if (demod_i2c->tuner == 1)
	demod_sta->ch_if = 36130;
    else if (demod_i2c->tuner == 2)
	demod_sta->ch_if = 4570;

    demod_sta->ch_bw     = (8-bw)*1000;
    demod_sta->symb_rate = 0; // TODO

    // Set Tuner
    if (ch_freq<1000 || ch_freq>900000) {
	    printk("Error: Invalid Channel Freq option %d, Skip Set tuner\n", ch_freq);
	    //ch_freq = 474000;
	    ret = -1;
    }
    else    {
  //  tuner_set_ch(demod_sta, demod_i2c);
    }


    if ((ch_freq%100)==2) {
    	printk("Input frequency is XXX002, Skip initial demod\n");
    }
    else{
    dvbt_reg_initial(demod_sta);
    }

    dvbt_enable_irq(7);// open symbolhead int

    tuner_type = demod_i2c->tuner;

    return ret;
}

static int dvbt_get_ch_power(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
    u32 ad_power;
    ad_power = agc_power_to_dbm((apb_read_reg(2, 0x1c)&0x7ff),apb_read_reg(2, 0x1b)&0x1ff,0,demod_i2c->tuner);
    return ad_power;
}

int dvbt_sfo(void)
{
    int sfo;
    sfo             =  apb_read_reg(2, 0x47)&0xfff    ;
    sfo             = (sfo>0x7ff)? (sfo - 0x1000): sfo;
    return (sfo);
}

int dvbt_fcfo(void)
{
    int fcfo;
    fcfo            = (apb_read_reg(2, 0x26))&0xffffff    ;
    fcfo            = (fcfo>0x7fffff)? (fcfo - 0x1000000): fcfo;
    return (fcfo);
}

static int dvbt_total_packet_error(void){return (apb_read_reg(2, 0xbf));}
static int dvbt_super_frame_counter(void){return (apb_read_reg(2, 0xc0)&0xfffff);}
static int dvbt_packet_correct_in_sframe(void){ return( apb_read_reg(2, 0xc1)&0xfffff );}
//static int dvbt_resync_counter(void){return((apb_read_reg(2, 0xc0)>>20)&0xff);}

static int dvbt_packets_per_sframe(void)
{
    u32 tmp;
    int hier_mode;
    int constel;
    int hp_code_rate;
    int lp_code_rate;
    int hier_sel;
    int code_rate;
    int ret;

    tmp = apb_read_reg(2, 0x06);
    constel = tmp>>13&3;
    hier_mode = tmp>>10&7;
    hp_code_rate = tmp>>7&7;
    lp_code_rate = tmp>>4&7;

    if (hier_mode == 0)	{
	code_rate = hp_code_rate;
    }
    else {
	tmp = apb_read_reg(2, 0x78);
	hier_sel = tmp>>9&1;
	if (hier_sel == 0) {
	    constel = 0; // QPSK;
	    code_rate = hp_code_rate;
	}
	else {
	    constel = constel==2 ? 1 : 0;
	    code_rate = lp_code_rate;
	}
    }

    switch(code_rate){
    case      0 : ret = (constel==0)?1008:(constel==1)?2016:3024; break;
    case      1 : ret = (constel==0)?1344:(constel==1)?2688:4032; break;
    case      2 : ret = (constel==0)?1512:(constel==1)?3024:4536; break;
    case      3 : ret = (constel==0)?1680:(constel==1)?3360:5040; break;
    case      4 : ret = (constel==0)?1764:(constel==1)?3528:5292; break;
    default: ret = (constel==0)?1008:(constel==1)?2016:3024; break;
    }
    return ret;
}

static int dvbt_get_per(void)
{
    int packets_per_sframe;
    int error;
    int per;

    packets_per_sframe = dvbt_packets_per_sframe();
    error = packets_per_sframe - dvbt_packet_correct_in_sframe();
    per = 1000*error/packets_per_sframe;

    return (per);
}

static void dvbt_set_test_bus(u8 sel)
{
    u32 tmp;

    tmp = apb_read_reg(2, 0x7f);
    tmp &= ~(0x1f);
    tmp |= ((1<<15) | (1<<5) | (sel&0x1f));
    apb_write_reg(2, 0x7f, tmp);
}
/*
void dvbt_get_test_out(u8 sel, u32 len, u32 *buf)
{
    int i;

    dvbt_set_test_bus(sel);

    for (i=0; i<len; i++) {
	buf[i] = apb_read_reg(2, 0x13);
    }
}
*/
void dvbt_get_test_out(u8 sel, u32 len, u32 *buf)
{
    int i, cnt;

    dvbt_set_test_bus(sel);

    for (i=0, cnt=0; i<len-4 && cnt<1000000; i++) {
	buf[i] = apb_read_reg(2, 0x13);
	if ((buf[i]>>10)&0x1) {
	    buf[i++] = apb_read_reg(2, 0x13);
	    buf[i++] = apb_read_reg(2, 0x13);
	    buf[i++] = apb_read_reg(2, 0x13);
	    buf[i++] = apb_read_reg(2, 0x13);
	    buf[i++] = apb_read_reg(2, 0x13);
	    buf[i++] = apb_read_reg(2, 0x13);
	    buf[i++] = apb_read_reg(2, 0x13);
	    buf[i++] = apb_read_reg(2, 0x13);
	}
	else {
	    i--;
	}

	cnt++;
    }
}

static int dvbt_get_avg_per(void)
{
    int packets_per_sframe;
    static int err_last;
    static int err_now;
    static int rsnum_now;
    static int rsnum_last;
    int per;

    packets_per_sframe = dvbt_packets_per_sframe();
    rsnum_last = rsnum_now;
    rsnum_now  = dvbt_super_frame_counter();
    err_last   = err_now;
    err_now    = dvbt_total_packet_error();
    if (rsnum_now != rsnum_last)
        per = 1000*(err_now - err_last) /
	    ((rsnum_now-rsnum_last)*packets_per_sframe);
    else
        per = 123;

    return (per);
}

int dvbt_status(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c,
		struct aml_demod_sts *demod_sts)
{
    // if parameters are needed to calc, pass the struct to func.
    // all small funcs like read_snr() should be static.

    demod_sts->ch_snr = apb_read_reg(2, 0x0a);
    demod_sts->ch_per = dvbt_get_per();
    demod_sts->ch_pow = dvbt_get_ch_power(demod_sta, demod_i2c);
    demod_sts->ch_ber = apb_read_reg(2, 0x0b);
    demod_sts->ch_sts = apb_read_reg(2, 0);
    demod_sts->dat0   = dvbt_get_avg_per();
    demod_sts->dat1   = apb_read_reg(2, 0x06);
    return 0;
}


static int dvbt_get_status(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	return apb_read_reg(2, 0x0)>>12&1;
}

static int dvbt_ber(void);

static int dvbt_get_ber(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	return dvbt_ber();/*unit: 1e-7*/
}

static int dvbt_get_snr(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	return apb_read_reg(2,0x0a)&0x3ff;/*dBm: bit0~bit2=decimal*/
}

static int dvbt_get_strength(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	int dbm = dvbt_get_ch_power(demod_sta, demod_i2c);
	return dbm;
}

static int dvbt_get_ucblocks(struct aml_demod_sta *demod_sta,
		struct aml_demod_i2c *demod_i2c)
{
	return dvbt_get_per();
}

struct demod_status_ops* dvbt_get_status_ops(void)
{
	static struct demod_status_ops ops = {
		.get_status = dvbt_get_status,
		.get_ber = dvbt_get_ber,
		.get_snr = dvbt_get_snr,
		.get_strength = dvbt_get_strength,
		.get_ucblocks = dvbt_get_ucblocks,
	};

	return &ops;
}

void dvbt_enable_irq(int dvbt_irq)
{
    // clear status & enable irq
    (*OFDM_INT_STS) &= ~(1<<dvbt_irq);
    (*OFDM_INT_EN) |= (1<<dvbt_irq);
}

void dvbt_disable_irq(int dvbt_irq)
{
    // disable irq & clear status
    (*OFDM_INT_EN) &= ~(1<<dvbt_irq);
    (*OFDM_INT_STS) &= ~(1<<dvbt_irq);
}

char *dvbt_irq_name[] = {
    "PFS_FCFO",
    "PFS_ICFO",
    " CS_FCFO",
    " PFS_SFO",
    " PFS_TPS",
    "      SP",
    "     CCI",
    "  Symbol",
    " In_Sync",
    "Out_Sync",
    "FSM Stat"};

void dvbt_isr(struct aml_demod_sta *demod_sta)
{
    u32 stat, mask;
    int dvbt_irq;

    stat = (*OFDM_INT_STS);
    mask = (*OFDM_INT_EN);
    stat &= mask;

    for (dvbt_irq=0; dvbt_irq<11; dvbt_irq++) {
	if (stat>>dvbt_irq&1) {
	    if (demod_sta->debug)
		printk("irq: aml_demod dvbt %2d %s %8x %8x\n",
		       dvbt_irq, dvbt_irq_name[dvbt_irq], stat, mask);
	    // dvbt_disable_irq(dvbt_irq);
	}
    }
    // clear status
    (*OFDM_INT_STS) = 0;
}


static int demod_monitor_ave(void);
int dvbt_isr_islock(void)
{
#define IN_SYNC_MASK (0x100)

    u32 stat, mask;

    stat = (*OFDM_INT_STS);
    *OFDM_INT_STS = stat & (~IN_SYNC_MASK);

    mask = (*OFDM_INT_EN);
    stat &= mask;

    return ((stat&IN_SYNC_MASK)==IN_SYNC_MASK);
}


int dvbt_isr_monitor(void)
{
#define SYM_HEAD_MASK (0x80)
	u32 stat, mask;

	stat = (*OFDM_INT_STS);
	*OFDM_INT_STS = stat & (~SYM_HEAD_MASK);

	mask = (*OFDM_INT_EN);
	stat &= mask;

	if ((stat&SYM_HEAD_MASK) == SYM_HEAD_MASK) // symbol_head int
		demod_monitor_ave();

	return 0;
}

int dvbt_isr_cancel(void)
{
	*OFDM_INT_STS = 0;
	*OFDM_INT_EN = 0;
	return 0;
}

static int demod_monitor_instant(void)
{
     int SNR;
     int SNR_SP          = 500;
     int SNR_TPS         = 0;
     int SNR_CP          = 0;
     int SFO_residual    = 0;
     int SFO_esti        = 0;
     int FCFO_esti       = 0;
     int FCFO_residual   = 0;
     int AGC_Gain        = 0;
     int be_vit_error    = 0;
     int Signal_power    = 0;
     int FECFlag         = 0;
     int EQ_seg_ratio    = 0;
     int tps_0           = 0;
     int tps_1           = 0;
     int tps_2           = 0;
     int cci_blank       = 0;

     int SFO                ;
     int FCFO               ;
     int timing_adj         ;
     int RS_CorrectNum      ;
     int RS_Error_sum       ;
     int resync_times       ;
     int tps_summary        ;

     int tps_window         ;
     int tps_guard          ;
     int tps_constell       ;
     int tps_Hier_none      ;
     int tps_Hier_alpha     ;
     int tps_HP_cr          ;
     int tps_LP_cr          ;


     int tmpAGCGain;

     // Read Registers
     SNR             = apb_read_reg(2,0x0a)              ;
     FECFlag         = (apb_read_reg(2,0x00)>>11)&0x3     ;
     SFO             = apb_read_reg(2,0x47)&0xfff        ;
     SFO_esti        = apb_read_reg(2,0x60)&0xfff        ;
     FCFO_esti       = (apb_read_reg(2,0x60)>>11)&0xfff   ;
     FCFO            = (apb_read_reg(2,0x26))&0xffffff    ;
     be_vit_error    = apb_read_reg(2,0x0c)&0x1fff       ;
     timing_adj      = apb_read_reg(2,0x6f)&0x1fff       ;
     RS_CorrectNum   = apb_read_reg(2,0xc1)&0xfffff      ;
     Signal_power    = (apb_read_reg(2,0x1b))&0x1ff       ;
     EQ_seg_ratio    = apb_read_reg(2,0x6e)&0x3ffff      ;
     tps_0           = apb_read_reg(2,0x64);
     tps_1           = apb_read_reg(2,0x65);
     tps_2           = apb_read_reg(2,0x66)&0xf;
     tps_summary     = apb_read_reg(2,0x04)&0x7fff;
     cci_blank       = (apb_read_reg(2,0x66)>>16);
     RS_Error_sum    = apb_read_reg(2,0xbf)&0x3ffff;
     resync_times    = (apb_read_reg(2,0xc0)>>20)&0xff;
     AGC_Gain        = apb_read_reg(2,0x1c)&0x7ff;

     // Calc
     SFO_residual    = (SFO>0x7ff)? (SFO - 0x1000): SFO;
     FCFO_residual   = (FCFO>0x7fffff)? (FCFO - 0x1000000): FCFO;
     FCFO_esti       = (FCFO_esti>0x7ff)?(FCFO_esti - 0x1000):FCFO_esti;
     SNR_CP          = (SNR)&0x3ff;
     SNR_TPS         = (SNR>>10)&0x3ff;
     SNR_SP          = (SNR>>20)&0x3ff;
     SNR_SP          = (SNR_SP  > 0x1ff)? SNR_SP - 0x400: SNR_SP;
     SNR_TPS         = (SNR_TPS > 0x1ff)? SNR_TPS- 0x400: SNR_TPS;
     SNR_CP          = (SNR_CP  > 0x1ff)? SNR_CP - 0x400: SNR_CP;
     tmpAGCGain      = AGC_Gain;
     timing_adj      = (timing_adj > 0xfff)? timing_adj - 0x2000: timing_adj;

     tps_window      = (tps_summary&0x3);
     tps_guard       = ((tps_summary>>2)&0x3);
     tps_constell    = ((tps_summary>>13)&0x3);
     tps_Hier_none   = (((tps_summary>>10)&0x7)==0)?1:0;
     tps_Hier_alpha  = (tps_summary>>11)&0x3;
     tps_Hier_alpha  = (tps_Hier_alpha==3)?4: tps_Hier_alpha;
     tps_LP_cr       = (tps_summary>>4)&0x7;
     tps_HP_cr       = (tps_summary>>7)&0x7;

     printk("\n\n");
     switch(tps_window){
     case 0: printk("2K ");break;
     case 1: printk("8K ");break;
     case 2: printk("4K ");break;
     default: printk("UnWin ");break;
    }
     switch(tps_guard){
     case 0: printk("1/32 ");break;
     case 1: printk("1/16 ");break;
     case 2: printk("1/ 8 ");break;
     case 3: printk("1/ 4 ");break;
     default: printk("UnGuard ");break;
     }
     switch(tps_constell){
     case 0: printk(" QPSK ");break;
     case 1: printk("16QAM ");break;
     case 2: printk("64QAM ");break;
     default: printk("UnConstl ");break;
     }
     switch(tps_Hier_none){
     case 0: printk("Hiera ");break;
     case 1: printk("non-H ");break;
     default: printk("UnHier ");break;
    }
     printk("%d ",tps_Hier_alpha);
     printk("HP ");
     switch(tps_HP_cr){
     case 0: printk("1/2 ");break;
     case 1: printk("2/3 ");break;
     case 2: printk("3/4 ");break;
     case 3: printk("5/6 ");break;
     case 4: printk("7/8 ");break;
     default: printk("UnHCr ");break;
     }
     printk("LP ");
     switch(tps_LP_cr){
     case 0: printk("1/2 ");break;
     case 1: printk("2/3 ");break;
     case 2: printk("3/4 ");break;
     case 3: printk("5/6 ");break;
     case 4: printk("7/8 ");break;
     default: printk("UnLCr ");break;
     }
     printk("\n");
     printk("P %4x ",RS_Error_sum);
     printk("SP %2d ",SNR_SP);
     printk("TPS %2d ",SNR_TPS);
     printk("CP %2d ",SNR_CP);
     printk("EQS %2x ",EQ_seg_ratio);
     printk("RSC %4d ",RS_CorrectNum);
     printk("SFO %3d ",SFO_residual);
     printk("FCFO %4d ",FCFO_residual);
     printk("Vit %3x ",be_vit_error);
     printk("Timing %3d ",timing_adj);
     printk("SigP %3x ",Signal_power);
     printk("AGC %d ",tmpAGCGain);
     printk("SigP %d ", agc_power_to_dbm(tmpAGCGain,  Signal_power,0, tuner_type));
     printk("FEC %x ",FECFlag);
     printk("ReSyn %x ",resync_times);
     printk("cciB %x",cci_blank);

     printk("\n");

     return 0;

}

int serial_div(int a, int b)
{
	 int c;
	 int cnt;
	 int b_buf;
	 if (b ==0)   return( 0x7fffffff);
	 if (a ==0)   return( 0);

	 c = 0;
	 cnt = 0;

	 a = (a <0)?-1*a:a;
	 b = (b <0)?-1*b:b;

	 b_buf = b;

	 while (a >= b){
	 	 b = b<<1;
	 	 cnt++;
 	}
 	while (b > b_buf){
 		  b = b >> 1;
 		  c = c << 1;
 		  if (a > b) {
 		  	c = c + 1;
 		  	a = a - b;
 		  }
 	}
 	return c;

}

static int ave0, bit_unit_L;

static int dvbt_ber(void)
{
	int BER_e_n7 = serial_div(ave0 * 40,bit_unit_L);
	return BER_e_n7;
}

static int demod_monitor_ave(void)
{
  	static int i=0;
  	static int ave[3]={0,0,0};

 	 ave[0] = ave[0] + (apb_read_reg(2,0x0b)&0x7ff);
 	 ave[1] = ave[1] + (apb_read_reg(2,0x0a)&0x3ff);
 	 ave[2] = ave[2] + (apb_read_reg(2,0x0c)&0x1fff);

 	 i ++;

	if (i >= 8192)
	{
		int tps_mode;
		int tps_constell;
		int r_t;
		int mode_L;
		int const_L;
		int SNR_Int;
		int SNR_fra;

		if(debug_amldvbt)
			demod_monitor_instant();

		r_t            = apb_read_reg(2,0x04);
		tps_mode       = r_t&0x3;
		tps_constell   = (r_t>>13)&0x3;
		mode_L         = (tps_mode==0)?1: (tps_mode==1)?4 :2;
		const_L        = (tps_constell==0)?2: (tps_constell==1)?4 :6;
		bit_unit_L     = 189 * mode_L * const_L;
		SNR_Int        = (ave[1]>>16);
		switch((ave[1]>>13)&0x7) {
		    case 0: SNR_fra = 0;break;
		    case 1: SNR_fra = 125;break;
		    case 2: SNR_fra = 250;break;
		    case 3: SNR_fra = 375;break;
		    case 4: SNR_fra = 500;break;
		    case 5: SNR_fra = 625;break;
		    case 6: SNR_fra = 750;break;
		    case 7: SNR_fra = 875;break;
		    default : SNR_fra = 0;break;
		}

		ave0 = ave[0];

		if(debug_amldvbt)
			printk("RSBi %d Thresh %d SNR %d.%d Vit %x \n\n",(ave[0]>>3)*5,(bit_unit_L*8),SNR_Int,SNR_fra,(ave[2]>>13));
		i = 0;
		ave[0]=ave[1]=ave[2]=0;

   }

   return i;
}

int dvbt_switch_to_HP(void)
{
	apb_write_reg(2, 0x78, apb_read_reg(2, 0x78) &~ (1<<9));
	return 0;
}

int dvbt_switch_to_LP(void)
{
	apb_write_reg(2, 0x78, apb_read_reg(2, 0x78) | (1<<9));
	return 0;
}

int dvbt_shutdown(void)
{
	apb_write_reg(2, 0x02, 0x00800000);  // SW reset bit[23] ; write anything to zero
	apb_write_reg(2, 0x00, 0x00000000);
	return 0;
}

int dvbt_get_params(struct aml_demod_sta *demod_sta,
		      struct aml_demod_i2c *adap,
		      	int  *code_rate_HP,  /* high priority stream code rate */
			int  *code_rate_LP,  /* low priority stream code rate */
			int  *constellation, /* modulation type (see above) */
			int  *transmission_mode,
			int  *guard_interval,
			int  *hierarchy_information )
{
	int tps_summary,tps_window,tps_guard,tps_constell,tps_Hier_none;
	int tps_Hier_alpha,tps_LP_cr,tps_HP_cr;

	tps_summary     = apb_read_reg(2,0x04)&0x7fff;

	tps_window      = (tps_summary&0x3);
	tps_guard       = ((tps_summary>>2)&0x3);
	tps_constell    = ((tps_summary>>13)&0x3);
	tps_Hier_none   = (((tps_summary>>10)&0x7)==0)?1:0;
	tps_Hier_alpha  = (tps_summary>>11)&0x3;
	tps_Hier_alpha  = (tps_Hier_alpha==3)?4: tps_Hier_alpha;
	tps_LP_cr       = (tps_summary>>4)&0x7;
	tps_HP_cr       = (tps_summary>>7)&0x7;

	if(code_rate_HP)
		*code_rate_HP = tps_HP_cr;/*1/2:2/3:3/4:5/6:7/8*/
	if(code_rate_LP)
		*code_rate_LP = tps_LP_cr;/*1/2:2/3:3/4:5/6:7/8*/
	if(constellation)
		*constellation = tps_constell;/*QPSK/16QAM/64QAM*/
	if(transmission_mode)
		*transmission_mode = tps_window;/*2K/8K/4K*/
	if(guard_interval)
		*guard_interval = tps_guard;/*1/32:1/16:1/8:1/4*/
	if(hierarchy_information)
		*hierarchy_information = tps_Hier_alpha;/*1/2/4*/

	return 0;
}


