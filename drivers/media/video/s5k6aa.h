#ifndef __S5K6AA_H__
#define __S5K6AA_H__

struct reginfo
{
    u16 reg;
    u16 val;
};

#define SEQUENCE_INIT        0x00
#define SEQUENCE_NORMAL      0x01
#define SEQUENCE_CAPTURE     0x02
#define SEQUENCE_PREVIEW     0x03

#define SEQUENCE_PROPERTY    0xFFF9
#define SEQUENCE_WAIT_MS     0xFFFA
#define SEQUENCE_WAIT_US     0xFFFB
#define SEQUENCE_WAIT_MS                (0xFFFE)
#define SEQUENCE_END                    (0xFFFF)
#define SEQUENCE_FAST_SETMODE_START     (0xFFFD)
#define SEQUENCE_FAST_SETMODE_END       (0xFFFC)


#endif