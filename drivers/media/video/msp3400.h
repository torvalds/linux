/*
 */

#ifndef MSP3400_H
#define MSP3400_H

/* ---------------------------------------------------------------------- */

struct msp_dfpreg {
    int reg;
    int value;
};

struct msp_matrix {
  int input;
  int output;
};

#define MSP_SET_DFPREG     _IOW('m',15,struct msp_dfpreg)
#define MSP_GET_DFPREG     _IOW('m',16,struct msp_dfpreg)

/* ioctl for MSP_SET_MATRIX will have to be registered */
#define MSP_SET_MATRIX     _IOW('m',17,struct msp_matrix)

#define SCART_MASK    0
#define SCART_IN1     1
#define SCART_IN2     2
#define SCART_IN1_DA  3
#define SCART_IN2_DA  4
#define SCART_IN3     5
#define SCART_IN4     6
#define SCART_MONO    7
#define SCART_MUTE    8

#define SCART_DSP_IN  0
#define SCART1_OUT    1
#define SCART2_OUT    2

#endif /* MSP3400_H */
