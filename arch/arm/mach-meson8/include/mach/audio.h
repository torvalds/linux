#ifndef __MACH_MESON_AUDIO_REGS_H
#define __MACH_MESON_AUDIO_REGS_H

#define I2SIN_DIR       0    // I2S CLK and LRCLK direction. 0 : input 1 : output.
#define I2SIN_CLK_SEL    1    // I2S clk selection : 0 : from pad input. 1 : from AIU.
#define I2SIN_LRCLK_SEL 2
#define I2SIN_POS_SYNC  3
#define I2SIN_LRCLK_SKEW 4    // 6:4
#define I2SIN_LRCLK_INVT 7
#define I2SIN_SIZE       8    //9:8 : 0 16 bit. 1 : 18 bits 2 : 20 bits 3 : 24bits.
#define I2SIN_CHAN_EN   10    //13:10. 
#define I2SIN_EN        15

#define AUDIN_FIFO0_EN       0
#define AUDIN_FIFO0_LOAD     2    //write 1 to load address to AUDIN_FIFO0.
         
#define AUDIN_FIFO0_DIN_SEL  3
            // 0     spdifIN
            // 1     i2Sin
            // 2     PCMIN
            // 3     HDMI in
            // 4     DEMODULATOR IN
#define AUDIN_FIFO0_ENDIAN   8    //10:8   data endian control.
#define AUDIN_FIFO0_CHAN     11    //14:11   channel number.  in M1 suppose there's only 1 channel and 2 channel.
#define AUDIN_FIFO0_UG       15    // urgent request enable.

#define AUDIN_FIFO1_EN       0
#define AUDIN_FIFO1_LOAD     2    //write 1 to load address to AUDIN_FIFO0.
         
#define AUDIN_FIFO1_DIN_SEL  3
            // 0     spdifIN
            // 1     i2Sin
            // 2     PCMIN
            // 3     HDMI in
            // 4     DEMODULATOR IN
#define AUDIN_FIFO1_ENDIAN   8    //10:8   data endian control.
#define AUDIN_FIFO1_CHAN     11    //14:11   channel number.  in M1 suppose there's only 1 channel and 2 channel.
#define AUDIN_FIFO1_UG       15    // urgent request enable.

#endif
