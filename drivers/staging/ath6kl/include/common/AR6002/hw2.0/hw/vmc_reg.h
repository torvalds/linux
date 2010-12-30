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

#define ADDR_ERROR_CONTROL_ADDRESS               0x00000200
#define ADDR_ERROR_CONTROL_OFFSET                0x00000200
#define ADDR_ERROR_CONTROL_QUAL_ENABLE_MSB       1
#define ADDR_ERROR_CONTROL_QUAL_ENABLE_LSB       1
#define ADDR_ERROR_CONTROL_QUAL_ENABLE_MASK      0x00000002
#define ADDR_ERROR_CONTROL_QUAL_ENABLE_GET(x)    (((x) & ADDR_ERROR_CONTROL_QUAL_ENABLE_MASK) >> ADDR_ERROR_CONTROL_QUAL_ENABLE_LSB)
#define ADDR_ERROR_CONTROL_QUAL_ENABLE_SET(x)    (((x) << ADDR_ERROR_CONTROL_QUAL_ENABLE_LSB) & ADDR_ERROR_CONTROL_QUAL_ENABLE_MASK)
#define ADDR_ERROR_CONTROL_ENABLE_MSB            0
#define ADDR_ERROR_CONTROL_ENABLE_LSB            0
#define ADDR_ERROR_CONTROL_ENABLE_MASK           0x00000001
#define ADDR_ERROR_CONTROL_ENABLE_GET(x)         (((x) & ADDR_ERROR_CONTROL_ENABLE_MASK) >> ADDR_ERROR_CONTROL_ENABLE_LSB)
#define ADDR_ERROR_CONTROL_ENABLE_SET(x)         (((x) << ADDR_ERROR_CONTROL_ENABLE_LSB) & ADDR_ERROR_CONTROL_ENABLE_MASK)

#define ADDR_ERROR_STATUS_ADDRESS                0x00000204
#define ADDR_ERROR_STATUS_OFFSET                 0x00000204
#define ADDR_ERROR_STATUS_WRITE_MSB              25
#define ADDR_ERROR_STATUS_WRITE_LSB              25
#define ADDR_ERROR_STATUS_WRITE_MASK             0x02000000
#define ADDR_ERROR_STATUS_WRITE_GET(x)           (((x) & ADDR_ERROR_STATUS_WRITE_MASK) >> ADDR_ERROR_STATUS_WRITE_LSB)
#define ADDR_ERROR_STATUS_WRITE_SET(x)           (((x) << ADDR_ERROR_STATUS_WRITE_LSB) & ADDR_ERROR_STATUS_WRITE_MASK)
#define ADDR_ERROR_STATUS_ADDRESS_MSB            24
#define ADDR_ERROR_STATUS_ADDRESS_LSB            0
#define ADDR_ERROR_STATUS_ADDRESS_MASK           0x01ffffff
#define ADDR_ERROR_STATUS_ADDRESS_GET(x)         (((x) & ADDR_ERROR_STATUS_ADDRESS_MASK) >> ADDR_ERROR_STATUS_ADDRESS_LSB)
#define ADDR_ERROR_STATUS_ADDRESS_SET(x)         (((x) << ADDR_ERROR_STATUS_ADDRESS_LSB) & ADDR_ERROR_STATUS_ADDRESS_MASK)


#ifndef __ASSEMBLER__

typedef struct vmc_reg_reg_s {
  volatile unsigned int mc_tcam_valid[32];
  volatile unsigned int mc_tcam_mask[32];
  volatile unsigned int mc_tcam_compare[32];
  volatile unsigned int mc_tcam_target[32];
  volatile unsigned int addr_error_control;
  volatile unsigned int addr_error_status;
} vmc_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _VMC_REG_H_ */
