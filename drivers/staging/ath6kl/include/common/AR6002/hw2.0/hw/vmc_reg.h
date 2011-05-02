#ifndef _VMC_REG_REG_H_
#define _VMC_REG_REG_H_

#define MC_TCAM_VALID_ADDRESS                    0x00000000
#define MC_TCAM_VALID_OFFSET                     0x00000000
#define MC_TCAM_VALID_BIT_MSB                    0
#define MC_TCAM_VALID_BIT_LSB                    0
#define MC_TCAM_VALID_BIT_MASK                   0x00000001
#define MC_TCAM_VALID_BIT_GET(x)                 (((x) & MC_TCAM_VALID_BIT_MASK) >> MC_TCAM_VALID_BIT_LSB)
#define MC_TCAM_VALID_BIT_SET(x)                 (((x) << MC_TCAM_VALID_BIT_LSB) & MC_TCAM_VALID_BIT_MASK)

#define MC_TCAM_MASK_ADDRESS                     0x00000080
#define MC_TCAM_MASK_OFFSET                      0x00000080
#define MC_TCAM_MASK_SIZE_MSB                    2
#define MC_TCAM_MASK_SIZE_LSB                    0
#define MC_TCAM_MASK_SIZE_MASK                   0x00000007
#define MC_TCAM_MASK_SIZE_GET(x)                 (((x) & MC_TCAM_MASK_SIZE_MASK) >> MC_TCAM_MASK_SIZE_LSB)
#define MC_TCAM_MASK_SIZE_SET(x)                 (((x) << MC_TCAM_MASK_SIZE_LSB) & MC_TCAM_MASK_SIZE_MASK)

#define MC_TCAM_COMPARE_ADDRESS                  0x00000100
#define MC_TCAM_COMPARE_OFFSET                   0x00000100
#define MC_TCAM_COMPARE_KEY_MSB                  21
#define MC_TCAM_COMPARE_KEY_LSB                  5
#define MC_TCAM_COMPARE_KEY_MASK                 0x003fffe0
#define MC_TCAM_COMPARE_KEY_GET(x)               (((x) & MC_TCAM_COMPARE_KEY_MASK) >> MC_TCAM_COMPARE_KEY_LSB)
#define MC_TCAM_COMPARE_KEY_SET(x)               (((x) << MC_TCAM_COMPARE_KEY_LSB) & MC_TCAM_COMPARE_KEY_MASK)

#define MC_TCAM_TARGET_ADDRESS                   0x00000180
#define MC_TCAM_TARGET_OFFSET                    0x00000180
#define MC_TCAM_TARGET_ADDR_MSB                  21
#define MC_TCAM_TARGET_ADDR_LSB                  5
#define MC_TCAM_TARGET_ADDR_MASK                 0x003fffe0
#define MC_TCAM_TARGET_ADDR_GET(x)               (((x) & MC_TCAM_TARGET_ADDR_MASK) >> MC_TCAM_TARGET_ADDR_LSB)
#define MC_TCAM_TARGET_ADDR_SET(x)               (((x) << MC_TCAM_TARGET_ADDR_LSB) & MC_TCAM_TARGET_ADDR_MASK)

#endif /* _VMC_REG_H_ */
