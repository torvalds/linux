#ifndef _ANALOG_INTF_REG_REG_H_
#define _ANALOG_INTF_REG_REG_H_

#define SW_OVERRIDE_ADDRESS                      0x00000080
#define SW_OVERRIDE_OFFSET                       0x00000080
#define SW_OVERRIDE_SUPDATE_DELAY_MSB            1
#define SW_OVERRIDE_SUPDATE_DELAY_LSB            1
#define SW_OVERRIDE_SUPDATE_DELAY_MASK           0x00000002
#define SW_OVERRIDE_SUPDATE_DELAY_GET(x)         (((x) & SW_OVERRIDE_SUPDATE_DELAY_MASK) >> SW_OVERRIDE_SUPDATE_DELAY_LSB)
#define SW_OVERRIDE_SUPDATE_DELAY_SET(x)         (((x) << SW_OVERRIDE_SUPDATE_DELAY_LSB) & SW_OVERRIDE_SUPDATE_DELAY_MASK)
#define SW_OVERRIDE_ENABLE_MSB                   0
#define SW_OVERRIDE_ENABLE_LSB                   0
#define SW_OVERRIDE_ENABLE_MASK                  0x00000001
#define SW_OVERRIDE_ENABLE_GET(x)                (((x) & SW_OVERRIDE_ENABLE_MASK) >> SW_OVERRIDE_ENABLE_LSB)
#define SW_OVERRIDE_ENABLE_SET(x)                (((x) << SW_OVERRIDE_ENABLE_LSB) & SW_OVERRIDE_ENABLE_MASK)

#define SIN_VAL_ADDRESS                          0x00000084
#define SIN_VAL_OFFSET                           0x00000084
#define SIN_VAL_SIN_MSB                          0
#define SIN_VAL_SIN_LSB                          0
#define SIN_VAL_SIN_MASK                         0x00000001
#define SIN_VAL_SIN_GET(x)                       (((x) & SIN_VAL_SIN_MASK) >> SIN_VAL_SIN_LSB)
#define SIN_VAL_SIN_SET(x)                       (((x) << SIN_VAL_SIN_LSB) & SIN_VAL_SIN_MASK)

#define SW_SCLK_ADDRESS                          0x00000088
#define SW_SCLK_OFFSET                           0x00000088
#define SW_SCLK_SW_SCLK_MSB                      0
#define SW_SCLK_SW_SCLK_LSB                      0
#define SW_SCLK_SW_SCLK_MASK                     0x00000001
#define SW_SCLK_SW_SCLK_GET(x)                   (((x) & SW_SCLK_SW_SCLK_MASK) >> SW_SCLK_SW_SCLK_LSB)
#define SW_SCLK_SW_SCLK_SET(x)                   (((x) << SW_SCLK_SW_SCLK_LSB) & SW_SCLK_SW_SCLK_MASK)

#define SW_CNTL_ADDRESS                          0x0000008c
#define SW_CNTL_OFFSET                           0x0000008c
#define SW_CNTL_SW_SCAPTURE_MSB                  2
#define SW_CNTL_SW_SCAPTURE_LSB                  2
#define SW_CNTL_SW_SCAPTURE_MASK                 0x00000004
#define SW_CNTL_SW_SCAPTURE_GET(x)               (((x) & SW_CNTL_SW_SCAPTURE_MASK) >> SW_CNTL_SW_SCAPTURE_LSB)
#define SW_CNTL_SW_SCAPTURE_SET(x)               (((x) << SW_CNTL_SW_SCAPTURE_LSB) & SW_CNTL_SW_SCAPTURE_MASK)
#define SW_CNTL_SW_SUPDATE_MSB                   1
#define SW_CNTL_SW_SUPDATE_LSB                   1
#define SW_CNTL_SW_SUPDATE_MASK                  0x00000002
#define SW_CNTL_SW_SUPDATE_GET(x)                (((x) & SW_CNTL_SW_SUPDATE_MASK) >> SW_CNTL_SW_SUPDATE_LSB)
#define SW_CNTL_SW_SUPDATE_SET(x)                (((x) << SW_CNTL_SW_SUPDATE_LSB) & SW_CNTL_SW_SUPDATE_MASK)
#define SW_CNTL_SW_SOUT_MSB                      0
#define SW_CNTL_SW_SOUT_LSB                      0
#define SW_CNTL_SW_SOUT_MASK                     0x00000001
#define SW_CNTL_SW_SOUT_GET(x)                   (((x) & SW_CNTL_SW_SOUT_MASK) >> SW_CNTL_SW_SOUT_LSB)
#define SW_CNTL_SW_SOUT_SET(x)                   (((x) << SW_CNTL_SW_SOUT_LSB) & SW_CNTL_SW_SOUT_MASK)


#ifndef __ASSEMBLER__

typedef struct analog_intf_reg_reg_s {
  unsigned char pad0[128]; /* pad to 0x80 */
  volatile unsigned int sw_override;
  volatile unsigned int sin_val;
  volatile unsigned int sw_sclk;
  volatile unsigned int sw_cntl;
} analog_intf_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _ANALOG_INTF_REG_H_ */
