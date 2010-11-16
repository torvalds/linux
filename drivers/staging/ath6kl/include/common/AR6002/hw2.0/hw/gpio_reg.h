#ifndef _GPIO_REG_REG_H_
#define _GPIO_REG_REG_H_

#define GPIO_OUT_ADDRESS                         0x00000000
#define GPIO_OUT_OFFSET                          0x00000000
#define GPIO_OUT_DATA_MSB                        17
#define GPIO_OUT_DATA_LSB                        0
#define GPIO_OUT_DATA_MASK                       0x0003ffff
#define GPIO_OUT_DATA_GET(x)                     (((x) & GPIO_OUT_DATA_MASK) >> GPIO_OUT_DATA_LSB)
#define GPIO_OUT_DATA_SET(x)                     (((x) << GPIO_OUT_DATA_LSB) & GPIO_OUT_DATA_MASK)

#define GPIO_OUT_W1TS_ADDRESS                    0x00000004
#define GPIO_OUT_W1TS_OFFSET                     0x00000004
#define GPIO_OUT_W1TS_DATA_MSB                   17
#define GPIO_OUT_W1TS_DATA_LSB                   0
#define GPIO_OUT_W1TS_DATA_MASK                  0x0003ffff
#define GPIO_OUT_W1TS_DATA_GET(x)                (((x) & GPIO_OUT_W1TS_DATA_MASK) >> GPIO_OUT_W1TS_DATA_LSB)
#define GPIO_OUT_W1TS_DATA_SET(x)                (((x) << GPIO_OUT_W1TS_DATA_LSB) & GPIO_OUT_W1TS_DATA_MASK)

#define GPIO_OUT_W1TC_ADDRESS                    0x00000008
#define GPIO_OUT_W1TC_OFFSET                     0x00000008
#define GPIO_OUT_W1TC_DATA_MSB                   17
#define GPIO_OUT_W1TC_DATA_LSB                   0
#define GPIO_OUT_W1TC_DATA_MASK                  0x0003ffff
#define GPIO_OUT_W1TC_DATA_GET(x)                (((x) & GPIO_OUT_W1TC_DATA_MASK) >> GPIO_OUT_W1TC_DATA_LSB)
#define GPIO_OUT_W1TC_DATA_SET(x)                (((x) << GPIO_OUT_W1TC_DATA_LSB) & GPIO_OUT_W1TC_DATA_MASK)

#define GPIO_ENABLE_ADDRESS                      0x0000000c
#define GPIO_ENABLE_OFFSET                       0x0000000c
#define GPIO_ENABLE_DATA_MSB                     17
#define GPIO_ENABLE_DATA_LSB                     0
#define GPIO_ENABLE_DATA_MASK                    0x0003ffff
#define GPIO_ENABLE_DATA_GET(x)                  (((x) & GPIO_ENABLE_DATA_MASK) >> GPIO_ENABLE_DATA_LSB)
#define GPIO_ENABLE_DATA_SET(x)                  (((x) << GPIO_ENABLE_DATA_LSB) & GPIO_ENABLE_DATA_MASK)

#define GPIO_ENABLE_W1TS_ADDRESS                 0x00000010
#define GPIO_ENABLE_W1TS_OFFSET                  0x00000010
#define GPIO_ENABLE_W1TS_DATA_MSB                17
#define GPIO_ENABLE_W1TS_DATA_LSB                0
#define GPIO_ENABLE_W1TS_DATA_MASK               0x0003ffff
#define GPIO_ENABLE_W1TS_DATA_GET(x)             (((x) & GPIO_ENABLE_W1TS_DATA_MASK) >> GPIO_ENABLE_W1TS_DATA_LSB)
#define GPIO_ENABLE_W1TS_DATA_SET(x)             (((x) << GPIO_ENABLE_W1TS_DATA_LSB) & GPIO_ENABLE_W1TS_DATA_MASK)

#define GPIO_ENABLE_W1TC_ADDRESS                 0x00000014
#define GPIO_ENABLE_W1TC_OFFSET                  0x00000014
#define GPIO_ENABLE_W1TC_DATA_MSB                17
#define GPIO_ENABLE_W1TC_DATA_LSB                0
#define GPIO_ENABLE_W1TC_DATA_MASK               0x0003ffff
#define GPIO_ENABLE_W1TC_DATA_GET(x)             (((x) & GPIO_ENABLE_W1TC_DATA_MASK) >> GPIO_ENABLE_W1TC_DATA_LSB)
#define GPIO_ENABLE_W1TC_DATA_SET(x)             (((x) << GPIO_ENABLE_W1TC_DATA_LSB) & GPIO_ENABLE_W1TC_DATA_MASK)

#define GPIO_IN_ADDRESS                          0x00000018
#define GPIO_IN_OFFSET                           0x00000018
#define GPIO_IN_DATA_MSB                         17
#define GPIO_IN_DATA_LSB                         0
#define GPIO_IN_DATA_MASK                        0x0003ffff
#define GPIO_IN_DATA_GET(x)                      (((x) & GPIO_IN_DATA_MASK) >> GPIO_IN_DATA_LSB)
#define GPIO_IN_DATA_SET(x)                      (((x) << GPIO_IN_DATA_LSB) & GPIO_IN_DATA_MASK)

#define GPIO_STATUS_ADDRESS                      0x0000001c
#define GPIO_STATUS_OFFSET                       0x0000001c
#define GPIO_STATUS_INTERRUPT_MSB                17
#define GPIO_STATUS_INTERRUPT_LSB                0
#define GPIO_STATUS_INTERRUPT_MASK               0x0003ffff
#define GPIO_STATUS_INTERRUPT_GET(x)             (((x) & GPIO_STATUS_INTERRUPT_MASK) >> GPIO_STATUS_INTERRUPT_LSB)
#define GPIO_STATUS_INTERRUPT_SET(x)             (((x) << GPIO_STATUS_INTERRUPT_LSB) & GPIO_STATUS_INTERRUPT_MASK)

#define GPIO_STATUS_W1TS_ADDRESS                 0x00000020
#define GPIO_STATUS_W1TS_OFFSET                  0x00000020
#define GPIO_STATUS_W1TS_INTERRUPT_MSB           17
#define GPIO_STATUS_W1TS_INTERRUPT_LSB           0
#define GPIO_STATUS_W1TS_INTERRUPT_MASK          0x0003ffff
#define GPIO_STATUS_W1TS_INTERRUPT_GET(x)        (((x) & GPIO_STATUS_W1TS_INTERRUPT_MASK) >> GPIO_STATUS_W1TS_INTERRUPT_LSB)
#define GPIO_STATUS_W1TS_INTERRUPT_SET(x)        (((x) << GPIO_STATUS_W1TS_INTERRUPT_LSB) & GPIO_STATUS_W1TS_INTERRUPT_MASK)

#define GPIO_STATUS_W1TC_ADDRESS                 0x00000024
#define GPIO_STATUS_W1TC_OFFSET                  0x00000024
#define GPIO_STATUS_W1TC_INTERRUPT_MSB           17
#define GPIO_STATUS_W1TC_INTERRUPT_LSB           0
#define GPIO_STATUS_W1TC_INTERRUPT_MASK          0x0003ffff
#define GPIO_STATUS_W1TC_INTERRUPT_GET(x)        (((x) & GPIO_STATUS_W1TC_INTERRUPT_MASK) >> GPIO_STATUS_W1TC_INTERRUPT_LSB)
#define GPIO_STATUS_W1TC_INTERRUPT_SET(x)        (((x) << GPIO_STATUS_W1TC_INTERRUPT_LSB) & GPIO_STATUS_W1TC_INTERRUPT_MASK)

#define GPIO_PIN0_ADDRESS                        0x00000028
#define GPIO_PIN0_OFFSET                         0x00000028
#define GPIO_PIN0_CONFIG_MSB                     12
#define GPIO_PIN0_CONFIG_LSB                     11
#define GPIO_PIN0_CONFIG_MASK                    0x00001800
#define GPIO_PIN0_CONFIG_GET(x)                  (((x) & GPIO_PIN0_CONFIG_MASK) >> GPIO_PIN0_CONFIG_LSB)
#define GPIO_PIN0_CONFIG_SET(x)                  (((x) << GPIO_PIN0_CONFIG_LSB) & GPIO_PIN0_CONFIG_MASK)
#define GPIO_PIN0_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN0_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN0_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN0_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN0_WAKEUP_ENABLE_MASK) >> GPIO_PIN0_WAKEUP_ENABLE_LSB)
#define GPIO_PIN0_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN0_WAKEUP_ENABLE_LSB) & GPIO_PIN0_WAKEUP_ENABLE_MASK)
#define GPIO_PIN0_INT_TYPE_MSB                   9
#define GPIO_PIN0_INT_TYPE_LSB                   7
#define GPIO_PIN0_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN0_INT_TYPE_GET(x)                (((x) & GPIO_PIN0_INT_TYPE_MASK) >> GPIO_PIN0_INT_TYPE_LSB)
#define GPIO_PIN0_INT_TYPE_SET(x)                (((x) << GPIO_PIN0_INT_TYPE_LSB) & GPIO_PIN0_INT_TYPE_MASK)
#define GPIO_PIN0_PAD_DRIVER_MSB                 2
#define GPIO_PIN0_PAD_DRIVER_LSB                 2
#define GPIO_PIN0_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN0_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN0_PAD_DRIVER_MASK) >> GPIO_PIN0_PAD_DRIVER_LSB)
#define GPIO_PIN0_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN0_PAD_DRIVER_LSB) & GPIO_PIN0_PAD_DRIVER_MASK)
#define GPIO_PIN0_SOURCE_MSB                     0
#define GPIO_PIN0_SOURCE_LSB                     0
#define GPIO_PIN0_SOURCE_MASK                    0x00000001
#define GPIO_PIN0_SOURCE_GET(x)                  (((x) & GPIO_PIN0_SOURCE_MASK) >> GPIO_PIN0_SOURCE_LSB)
#define GPIO_PIN0_SOURCE_SET(x)                  (((x) << GPIO_PIN0_SOURCE_LSB) & GPIO_PIN0_SOURCE_MASK)

#define GPIO_PIN1_ADDRESS                        0x0000002c
#define GPIO_PIN1_OFFSET                         0x0000002c
#define GPIO_PIN1_CONFIG_MSB                     12
#define GPIO_PIN1_CONFIG_LSB                     11
#define GPIO_PIN1_CONFIG_MASK                    0x00001800
#define GPIO_PIN1_CONFIG_GET(x)                  (((x) & GPIO_PIN1_CONFIG_MASK) >> GPIO_PIN1_CONFIG_LSB)
#define GPIO_PIN1_CONFIG_SET(x)                  (((x) << GPIO_PIN1_CONFIG_LSB) & GPIO_PIN1_CONFIG_MASK)
#define GPIO_PIN1_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN1_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN1_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN1_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN1_WAKEUP_ENABLE_MASK) >> GPIO_PIN1_WAKEUP_ENABLE_LSB)
#define GPIO_PIN1_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN1_WAKEUP_ENABLE_LSB) & GPIO_PIN1_WAKEUP_ENABLE_MASK)
#define GPIO_PIN1_INT_TYPE_MSB                   9
#define GPIO_PIN1_INT_TYPE_LSB                   7
#define GPIO_PIN1_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN1_INT_TYPE_GET(x)                (((x) & GPIO_PIN1_INT_TYPE_MASK) >> GPIO_PIN1_INT_TYPE_LSB)
#define GPIO_PIN1_INT_TYPE_SET(x)                (((x) << GPIO_PIN1_INT_TYPE_LSB) & GPIO_PIN1_INT_TYPE_MASK)
#define GPIO_PIN1_PAD_DRIVER_MSB                 2
#define GPIO_PIN1_PAD_DRIVER_LSB                 2
#define GPIO_PIN1_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN1_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN1_PAD_DRIVER_MASK) >> GPIO_PIN1_PAD_DRIVER_LSB)
#define GPIO_PIN1_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN1_PAD_DRIVER_LSB) & GPIO_PIN1_PAD_DRIVER_MASK)
#define GPIO_PIN1_SOURCE_MSB                     0
#define GPIO_PIN1_SOURCE_LSB                     0
#define GPIO_PIN1_SOURCE_MASK                    0x00000001
#define GPIO_PIN1_SOURCE_GET(x)                  (((x) & GPIO_PIN1_SOURCE_MASK) >> GPIO_PIN1_SOURCE_LSB)
#define GPIO_PIN1_SOURCE_SET(x)                  (((x) << GPIO_PIN1_SOURCE_LSB) & GPIO_PIN1_SOURCE_MASK)

#define GPIO_PIN2_ADDRESS                        0x00000030
#define GPIO_PIN2_OFFSET                         0x00000030
#define GPIO_PIN2_CONFIG_MSB                     12
#define GPIO_PIN2_CONFIG_LSB                     11
#define GPIO_PIN2_CONFIG_MASK                    0x00001800
#define GPIO_PIN2_CONFIG_GET(x)                  (((x) & GPIO_PIN2_CONFIG_MASK) >> GPIO_PIN2_CONFIG_LSB)
#define GPIO_PIN2_CONFIG_SET(x)                  (((x) << GPIO_PIN2_CONFIG_LSB) & GPIO_PIN2_CONFIG_MASK)
#define GPIO_PIN2_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN2_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN2_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN2_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN2_WAKEUP_ENABLE_MASK) >> GPIO_PIN2_WAKEUP_ENABLE_LSB)
#define GPIO_PIN2_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN2_WAKEUP_ENABLE_LSB) & GPIO_PIN2_WAKEUP_ENABLE_MASK)
#define GPIO_PIN2_INT_TYPE_MSB                   9
#define GPIO_PIN2_INT_TYPE_LSB                   7
#define GPIO_PIN2_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN2_INT_TYPE_GET(x)                (((x) & GPIO_PIN2_INT_TYPE_MASK) >> GPIO_PIN2_INT_TYPE_LSB)
#define GPIO_PIN2_INT_TYPE_SET(x)                (((x) << GPIO_PIN2_INT_TYPE_LSB) & GPIO_PIN2_INT_TYPE_MASK)
#define GPIO_PIN2_PAD_DRIVER_MSB                 2
#define GPIO_PIN2_PAD_DRIVER_LSB                 2
#define GPIO_PIN2_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN2_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN2_PAD_DRIVER_MASK) >> GPIO_PIN2_PAD_DRIVER_LSB)
#define GPIO_PIN2_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN2_PAD_DRIVER_LSB) & GPIO_PIN2_PAD_DRIVER_MASK)
#define GPIO_PIN2_SOURCE_MSB                     0
#define GPIO_PIN2_SOURCE_LSB                     0
#define GPIO_PIN2_SOURCE_MASK                    0x00000001
#define GPIO_PIN2_SOURCE_GET(x)                  (((x) & GPIO_PIN2_SOURCE_MASK) >> GPIO_PIN2_SOURCE_LSB)
#define GPIO_PIN2_SOURCE_SET(x)                  (((x) << GPIO_PIN2_SOURCE_LSB) & GPIO_PIN2_SOURCE_MASK)

#define GPIO_PIN3_ADDRESS                        0x00000034
#define GPIO_PIN3_OFFSET                         0x00000034
#define GPIO_PIN3_CONFIG_MSB                     12
#define GPIO_PIN3_CONFIG_LSB                     11
#define GPIO_PIN3_CONFIG_MASK                    0x00001800
#define GPIO_PIN3_CONFIG_GET(x)                  (((x) & GPIO_PIN3_CONFIG_MASK) >> GPIO_PIN3_CONFIG_LSB)
#define GPIO_PIN3_CONFIG_SET(x)                  (((x) << GPIO_PIN3_CONFIG_LSB) & GPIO_PIN3_CONFIG_MASK)
#define GPIO_PIN3_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN3_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN3_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN3_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN3_WAKEUP_ENABLE_MASK) >> GPIO_PIN3_WAKEUP_ENABLE_LSB)
#define GPIO_PIN3_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN3_WAKEUP_ENABLE_LSB) & GPIO_PIN3_WAKEUP_ENABLE_MASK)
#define GPIO_PIN3_INT_TYPE_MSB                   9
#define GPIO_PIN3_INT_TYPE_LSB                   7
#define GPIO_PIN3_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN3_INT_TYPE_GET(x)                (((x) & GPIO_PIN3_INT_TYPE_MASK) >> GPIO_PIN3_INT_TYPE_LSB)
#define GPIO_PIN3_INT_TYPE_SET(x)                (((x) << GPIO_PIN3_INT_TYPE_LSB) & GPIO_PIN3_INT_TYPE_MASK)
#define GPIO_PIN3_PAD_DRIVER_MSB                 2
#define GPIO_PIN3_PAD_DRIVER_LSB                 2
#define GPIO_PIN3_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN3_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN3_PAD_DRIVER_MASK) >> GPIO_PIN3_PAD_DRIVER_LSB)
#define GPIO_PIN3_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN3_PAD_DRIVER_LSB) & GPIO_PIN3_PAD_DRIVER_MASK)
#define GPIO_PIN3_SOURCE_MSB                     0
#define GPIO_PIN3_SOURCE_LSB                     0
#define GPIO_PIN3_SOURCE_MASK                    0x00000001
#define GPIO_PIN3_SOURCE_GET(x)                  (((x) & GPIO_PIN3_SOURCE_MASK) >> GPIO_PIN3_SOURCE_LSB)
#define GPIO_PIN3_SOURCE_SET(x)                  (((x) << GPIO_PIN3_SOURCE_LSB) & GPIO_PIN3_SOURCE_MASK)

#define GPIO_PIN4_ADDRESS                        0x00000038
#define GPIO_PIN4_OFFSET                         0x00000038
#define GPIO_PIN4_CONFIG_MSB                     12
#define GPIO_PIN4_CONFIG_LSB                     11
#define GPIO_PIN4_CONFIG_MASK                    0x00001800
#define GPIO_PIN4_CONFIG_GET(x)                  (((x) & GPIO_PIN4_CONFIG_MASK) >> GPIO_PIN4_CONFIG_LSB)
#define GPIO_PIN4_CONFIG_SET(x)                  (((x) << GPIO_PIN4_CONFIG_LSB) & GPIO_PIN4_CONFIG_MASK)
#define GPIO_PIN4_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN4_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN4_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN4_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN4_WAKEUP_ENABLE_MASK) >> GPIO_PIN4_WAKEUP_ENABLE_LSB)
#define GPIO_PIN4_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN4_WAKEUP_ENABLE_LSB) & GPIO_PIN4_WAKEUP_ENABLE_MASK)
#define GPIO_PIN4_INT_TYPE_MSB                   9
#define GPIO_PIN4_INT_TYPE_LSB                   7
#define GPIO_PIN4_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN4_INT_TYPE_GET(x)                (((x) & GPIO_PIN4_INT_TYPE_MASK) >> GPIO_PIN4_INT_TYPE_LSB)
#define GPIO_PIN4_INT_TYPE_SET(x)                (((x) << GPIO_PIN4_INT_TYPE_LSB) & GPIO_PIN4_INT_TYPE_MASK)
#define GPIO_PIN4_PAD_DRIVER_MSB                 2
#define GPIO_PIN4_PAD_DRIVER_LSB                 2
#define GPIO_PIN4_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN4_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN4_PAD_DRIVER_MASK) >> GPIO_PIN4_PAD_DRIVER_LSB)
#define GPIO_PIN4_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN4_PAD_DRIVER_LSB) & GPIO_PIN4_PAD_DRIVER_MASK)
#define GPIO_PIN4_SOURCE_MSB                     0
#define GPIO_PIN4_SOURCE_LSB                     0
#define GPIO_PIN4_SOURCE_MASK                    0x00000001
#define GPIO_PIN4_SOURCE_GET(x)                  (((x) & GPIO_PIN4_SOURCE_MASK) >> GPIO_PIN4_SOURCE_LSB)
#define GPIO_PIN4_SOURCE_SET(x)                  (((x) << GPIO_PIN4_SOURCE_LSB) & GPIO_PIN4_SOURCE_MASK)

#define GPIO_PIN5_ADDRESS                        0x0000003c
#define GPIO_PIN5_OFFSET                         0x0000003c
#define GPIO_PIN5_CONFIG_MSB                     12
#define GPIO_PIN5_CONFIG_LSB                     11
#define GPIO_PIN5_CONFIG_MASK                    0x00001800
#define GPIO_PIN5_CONFIG_GET(x)                  (((x) & GPIO_PIN5_CONFIG_MASK) >> GPIO_PIN5_CONFIG_LSB)
#define GPIO_PIN5_CONFIG_SET(x)                  (((x) << GPIO_PIN5_CONFIG_LSB) & GPIO_PIN5_CONFIG_MASK)
#define GPIO_PIN5_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN5_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN5_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN5_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN5_WAKEUP_ENABLE_MASK) >> GPIO_PIN5_WAKEUP_ENABLE_LSB)
#define GPIO_PIN5_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN5_WAKEUP_ENABLE_LSB) & GPIO_PIN5_WAKEUP_ENABLE_MASK)
#define GPIO_PIN5_INT_TYPE_MSB                   9
#define GPIO_PIN5_INT_TYPE_LSB                   7
#define GPIO_PIN5_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN5_INT_TYPE_GET(x)                (((x) & GPIO_PIN5_INT_TYPE_MASK) >> GPIO_PIN5_INT_TYPE_LSB)
#define GPIO_PIN5_INT_TYPE_SET(x)                (((x) << GPIO_PIN5_INT_TYPE_LSB) & GPIO_PIN5_INT_TYPE_MASK)
#define GPIO_PIN5_PAD_DRIVER_MSB                 2
#define GPIO_PIN5_PAD_DRIVER_LSB                 2
#define GPIO_PIN5_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN5_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN5_PAD_DRIVER_MASK) >> GPIO_PIN5_PAD_DRIVER_LSB)
#define GPIO_PIN5_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN5_PAD_DRIVER_LSB) & GPIO_PIN5_PAD_DRIVER_MASK)
#define GPIO_PIN5_SOURCE_MSB                     0
#define GPIO_PIN5_SOURCE_LSB                     0
#define GPIO_PIN5_SOURCE_MASK                    0x00000001
#define GPIO_PIN5_SOURCE_GET(x)                  (((x) & GPIO_PIN5_SOURCE_MASK) >> GPIO_PIN5_SOURCE_LSB)
#define GPIO_PIN5_SOURCE_SET(x)                  (((x) << GPIO_PIN5_SOURCE_LSB) & GPIO_PIN5_SOURCE_MASK)

#define GPIO_PIN6_ADDRESS                        0x00000040
#define GPIO_PIN6_OFFSET                         0x00000040
#define GPIO_PIN6_CONFIG_MSB                     12
#define GPIO_PIN6_CONFIG_LSB                     11
#define GPIO_PIN6_CONFIG_MASK                    0x00001800
#define GPIO_PIN6_CONFIG_GET(x)                  (((x) & GPIO_PIN6_CONFIG_MASK) >> GPIO_PIN6_CONFIG_LSB)
#define GPIO_PIN6_CONFIG_SET(x)                  (((x) << GPIO_PIN6_CONFIG_LSB) & GPIO_PIN6_CONFIG_MASK)
#define GPIO_PIN6_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN6_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN6_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN6_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN6_WAKEUP_ENABLE_MASK) >> GPIO_PIN6_WAKEUP_ENABLE_LSB)
#define GPIO_PIN6_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN6_WAKEUP_ENABLE_LSB) & GPIO_PIN6_WAKEUP_ENABLE_MASK)
#define GPIO_PIN6_INT_TYPE_MSB                   9
#define GPIO_PIN6_INT_TYPE_LSB                   7
#define GPIO_PIN6_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN6_INT_TYPE_GET(x)                (((x) & GPIO_PIN6_INT_TYPE_MASK) >> GPIO_PIN6_INT_TYPE_LSB)
#define GPIO_PIN6_INT_TYPE_SET(x)                (((x) << GPIO_PIN6_INT_TYPE_LSB) & GPIO_PIN6_INT_TYPE_MASK)
#define GPIO_PIN6_PAD_DRIVER_MSB                 2
#define GPIO_PIN6_PAD_DRIVER_LSB                 2
#define GPIO_PIN6_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN6_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN6_PAD_DRIVER_MASK) >> GPIO_PIN6_PAD_DRIVER_LSB)
#define GPIO_PIN6_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN6_PAD_DRIVER_LSB) & GPIO_PIN6_PAD_DRIVER_MASK)
#define GPIO_PIN6_SOURCE_MSB                     0
#define GPIO_PIN6_SOURCE_LSB                     0
#define GPIO_PIN6_SOURCE_MASK                    0x00000001
#define GPIO_PIN6_SOURCE_GET(x)                  (((x) & GPIO_PIN6_SOURCE_MASK) >> GPIO_PIN6_SOURCE_LSB)
#define GPIO_PIN6_SOURCE_SET(x)                  (((x) << GPIO_PIN6_SOURCE_LSB) & GPIO_PIN6_SOURCE_MASK)

#define GPIO_PIN7_ADDRESS                        0x00000044
#define GPIO_PIN7_OFFSET                         0x00000044
#define GPIO_PIN7_CONFIG_MSB                     12
#define GPIO_PIN7_CONFIG_LSB                     11
#define GPIO_PIN7_CONFIG_MASK                    0x00001800
#define GPIO_PIN7_CONFIG_GET(x)                  (((x) & GPIO_PIN7_CONFIG_MASK) >> GPIO_PIN7_CONFIG_LSB)
#define GPIO_PIN7_CONFIG_SET(x)                  (((x) << GPIO_PIN7_CONFIG_LSB) & GPIO_PIN7_CONFIG_MASK)
#define GPIO_PIN7_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN7_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN7_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN7_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN7_WAKEUP_ENABLE_MASK) >> GPIO_PIN7_WAKEUP_ENABLE_LSB)
#define GPIO_PIN7_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN7_WAKEUP_ENABLE_LSB) & GPIO_PIN7_WAKEUP_ENABLE_MASK)
#define GPIO_PIN7_INT_TYPE_MSB                   9
#define GPIO_PIN7_INT_TYPE_LSB                   7
#define GPIO_PIN7_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN7_INT_TYPE_GET(x)                (((x) & GPIO_PIN7_INT_TYPE_MASK) >> GPIO_PIN7_INT_TYPE_LSB)
#define GPIO_PIN7_INT_TYPE_SET(x)                (((x) << GPIO_PIN7_INT_TYPE_LSB) & GPIO_PIN7_INT_TYPE_MASK)
#define GPIO_PIN7_PAD_DRIVER_MSB                 2
#define GPIO_PIN7_PAD_DRIVER_LSB                 2
#define GPIO_PIN7_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN7_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN7_PAD_DRIVER_MASK) >> GPIO_PIN7_PAD_DRIVER_LSB)
#define GPIO_PIN7_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN7_PAD_DRIVER_LSB) & GPIO_PIN7_PAD_DRIVER_MASK)
#define GPIO_PIN7_SOURCE_MSB                     0
#define GPIO_PIN7_SOURCE_LSB                     0
#define GPIO_PIN7_SOURCE_MASK                    0x00000001
#define GPIO_PIN7_SOURCE_GET(x)                  (((x) & GPIO_PIN7_SOURCE_MASK) >> GPIO_PIN7_SOURCE_LSB)
#define GPIO_PIN7_SOURCE_SET(x)                  (((x) << GPIO_PIN7_SOURCE_LSB) & GPIO_PIN7_SOURCE_MASK)

#define GPIO_PIN8_ADDRESS                        0x00000048
#define GPIO_PIN8_OFFSET                         0x00000048
#define GPIO_PIN8_CONFIG_MSB                     12
#define GPIO_PIN8_CONFIG_LSB                     11
#define GPIO_PIN8_CONFIG_MASK                    0x00001800
#define GPIO_PIN8_CONFIG_GET(x)                  (((x) & GPIO_PIN8_CONFIG_MASK) >> GPIO_PIN8_CONFIG_LSB)
#define GPIO_PIN8_CONFIG_SET(x)                  (((x) << GPIO_PIN8_CONFIG_LSB) & GPIO_PIN8_CONFIG_MASK)
#define GPIO_PIN8_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN8_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN8_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN8_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN8_WAKEUP_ENABLE_MASK) >> GPIO_PIN8_WAKEUP_ENABLE_LSB)
#define GPIO_PIN8_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN8_WAKEUP_ENABLE_LSB) & GPIO_PIN8_WAKEUP_ENABLE_MASK)
#define GPIO_PIN8_INT_TYPE_MSB                   9
#define GPIO_PIN8_INT_TYPE_LSB                   7
#define GPIO_PIN8_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN8_INT_TYPE_GET(x)                (((x) & GPIO_PIN8_INT_TYPE_MASK) >> GPIO_PIN8_INT_TYPE_LSB)
#define GPIO_PIN8_INT_TYPE_SET(x)                (((x) << GPIO_PIN8_INT_TYPE_LSB) & GPIO_PIN8_INT_TYPE_MASK)
#define GPIO_PIN8_PAD_DRIVER_MSB                 2
#define GPIO_PIN8_PAD_DRIVER_LSB                 2
#define GPIO_PIN8_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN8_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN8_PAD_DRIVER_MASK) >> GPIO_PIN8_PAD_DRIVER_LSB)
#define GPIO_PIN8_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN8_PAD_DRIVER_LSB) & GPIO_PIN8_PAD_DRIVER_MASK)
#define GPIO_PIN8_SOURCE_MSB                     0
#define GPIO_PIN8_SOURCE_LSB                     0
#define GPIO_PIN8_SOURCE_MASK                    0x00000001
#define GPIO_PIN8_SOURCE_GET(x)                  (((x) & GPIO_PIN8_SOURCE_MASK) >> GPIO_PIN8_SOURCE_LSB)
#define GPIO_PIN8_SOURCE_SET(x)                  (((x) << GPIO_PIN8_SOURCE_LSB) & GPIO_PIN8_SOURCE_MASK)

#define GPIO_PIN9_ADDRESS                        0x0000004c
#define GPIO_PIN9_OFFSET                         0x0000004c
#define GPIO_PIN9_CONFIG_MSB                     12
#define GPIO_PIN9_CONFIG_LSB                     11
#define GPIO_PIN9_CONFIG_MASK                    0x00001800
#define GPIO_PIN9_CONFIG_GET(x)                  (((x) & GPIO_PIN9_CONFIG_MASK) >> GPIO_PIN9_CONFIG_LSB)
#define GPIO_PIN9_CONFIG_SET(x)                  (((x) << GPIO_PIN9_CONFIG_LSB) & GPIO_PIN9_CONFIG_MASK)
#define GPIO_PIN9_WAKEUP_ENABLE_MSB              10
#define GPIO_PIN9_WAKEUP_ENABLE_LSB              10
#define GPIO_PIN9_WAKEUP_ENABLE_MASK             0x00000400
#define GPIO_PIN9_WAKEUP_ENABLE_GET(x)           (((x) & GPIO_PIN9_WAKEUP_ENABLE_MASK) >> GPIO_PIN9_WAKEUP_ENABLE_LSB)
#define GPIO_PIN9_WAKEUP_ENABLE_SET(x)           (((x) << GPIO_PIN9_WAKEUP_ENABLE_LSB) & GPIO_PIN9_WAKEUP_ENABLE_MASK)
#define GPIO_PIN9_INT_TYPE_MSB                   9
#define GPIO_PIN9_INT_TYPE_LSB                   7
#define GPIO_PIN9_INT_TYPE_MASK                  0x00000380
#define GPIO_PIN9_INT_TYPE_GET(x)                (((x) & GPIO_PIN9_INT_TYPE_MASK) >> GPIO_PIN9_INT_TYPE_LSB)
#define GPIO_PIN9_INT_TYPE_SET(x)                (((x) << GPIO_PIN9_INT_TYPE_LSB) & GPIO_PIN9_INT_TYPE_MASK)
#define GPIO_PIN9_PAD_DRIVER_MSB                 2
#define GPIO_PIN9_PAD_DRIVER_LSB                 2
#define GPIO_PIN9_PAD_DRIVER_MASK                0x00000004
#define GPIO_PIN9_PAD_DRIVER_GET(x)              (((x) & GPIO_PIN9_PAD_DRIVER_MASK) >> GPIO_PIN9_PAD_DRIVER_LSB)
#define GPIO_PIN9_PAD_DRIVER_SET(x)              (((x) << GPIO_PIN9_PAD_DRIVER_LSB) & GPIO_PIN9_PAD_DRIVER_MASK)
#define GPIO_PIN9_SOURCE_MSB                     0
#define GPIO_PIN9_SOURCE_LSB                     0
#define GPIO_PIN9_SOURCE_MASK                    0x00000001
#define GPIO_PIN9_SOURCE_GET(x)                  (((x) & GPIO_PIN9_SOURCE_MASK) >> GPIO_PIN9_SOURCE_LSB)
#define GPIO_PIN9_SOURCE_SET(x)                  (((x) << GPIO_PIN9_SOURCE_LSB) & GPIO_PIN9_SOURCE_MASK)

#define GPIO_PIN10_ADDRESS                       0x00000050
#define GPIO_PIN10_OFFSET                        0x00000050
#define GPIO_PIN10_CONFIG_MSB                    12
#define GPIO_PIN10_CONFIG_LSB                    11
#define GPIO_PIN10_CONFIG_MASK                   0x00001800
#define GPIO_PIN10_CONFIG_GET(x)                 (((x) & GPIO_PIN10_CONFIG_MASK) >> GPIO_PIN10_CONFIG_LSB)
#define GPIO_PIN10_CONFIG_SET(x)                 (((x) << GPIO_PIN10_CONFIG_LSB) & GPIO_PIN10_CONFIG_MASK)
#define GPIO_PIN10_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN10_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN10_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN10_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN10_WAKEUP_ENABLE_MASK) >> GPIO_PIN10_WAKEUP_ENABLE_LSB)
#define GPIO_PIN10_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN10_WAKEUP_ENABLE_LSB) & GPIO_PIN10_WAKEUP_ENABLE_MASK)
#define GPIO_PIN10_INT_TYPE_MSB                  9
#define GPIO_PIN10_INT_TYPE_LSB                  7
#define GPIO_PIN10_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN10_INT_TYPE_GET(x)               (((x) & GPIO_PIN10_INT_TYPE_MASK) >> GPIO_PIN10_INT_TYPE_LSB)
#define GPIO_PIN10_INT_TYPE_SET(x)               (((x) << GPIO_PIN10_INT_TYPE_LSB) & GPIO_PIN10_INT_TYPE_MASK)
#define GPIO_PIN10_PAD_DRIVER_MSB                2
#define GPIO_PIN10_PAD_DRIVER_LSB                2
#define GPIO_PIN10_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN10_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN10_PAD_DRIVER_MASK) >> GPIO_PIN10_PAD_DRIVER_LSB)
#define GPIO_PIN10_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN10_PAD_DRIVER_LSB) & GPIO_PIN10_PAD_DRIVER_MASK)
#define GPIO_PIN10_SOURCE_MSB                    0
#define GPIO_PIN10_SOURCE_LSB                    0
#define GPIO_PIN10_SOURCE_MASK                   0x00000001
#define GPIO_PIN10_SOURCE_GET(x)                 (((x) & GPIO_PIN10_SOURCE_MASK) >> GPIO_PIN10_SOURCE_LSB)
#define GPIO_PIN10_SOURCE_SET(x)                 (((x) << GPIO_PIN10_SOURCE_LSB) & GPIO_PIN10_SOURCE_MASK)

#define GPIO_PIN11_ADDRESS                       0x00000054
#define GPIO_PIN11_OFFSET                        0x00000054
#define GPIO_PIN11_CONFIG_MSB                    12
#define GPIO_PIN11_CONFIG_LSB                    11
#define GPIO_PIN11_CONFIG_MASK                   0x00001800
#define GPIO_PIN11_CONFIG_GET(x)                 (((x) & GPIO_PIN11_CONFIG_MASK) >> GPIO_PIN11_CONFIG_LSB)
#define GPIO_PIN11_CONFIG_SET(x)                 (((x) << GPIO_PIN11_CONFIG_LSB) & GPIO_PIN11_CONFIG_MASK)
#define GPIO_PIN11_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN11_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN11_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN11_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN11_WAKEUP_ENABLE_MASK) >> GPIO_PIN11_WAKEUP_ENABLE_LSB)
#define GPIO_PIN11_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN11_WAKEUP_ENABLE_LSB) & GPIO_PIN11_WAKEUP_ENABLE_MASK)
#define GPIO_PIN11_INT_TYPE_MSB                  9
#define GPIO_PIN11_INT_TYPE_LSB                  7
#define GPIO_PIN11_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN11_INT_TYPE_GET(x)               (((x) & GPIO_PIN11_INT_TYPE_MASK) >> GPIO_PIN11_INT_TYPE_LSB)
#define GPIO_PIN11_INT_TYPE_SET(x)               (((x) << GPIO_PIN11_INT_TYPE_LSB) & GPIO_PIN11_INT_TYPE_MASK)
#define GPIO_PIN11_PAD_DRIVER_MSB                2
#define GPIO_PIN11_PAD_DRIVER_LSB                2
#define GPIO_PIN11_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN11_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN11_PAD_DRIVER_MASK) >> GPIO_PIN11_PAD_DRIVER_LSB)
#define GPIO_PIN11_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN11_PAD_DRIVER_LSB) & GPIO_PIN11_PAD_DRIVER_MASK)
#define GPIO_PIN11_SOURCE_MSB                    0
#define GPIO_PIN11_SOURCE_LSB                    0
#define GPIO_PIN11_SOURCE_MASK                   0x00000001
#define GPIO_PIN11_SOURCE_GET(x)                 (((x) & GPIO_PIN11_SOURCE_MASK) >> GPIO_PIN11_SOURCE_LSB)
#define GPIO_PIN11_SOURCE_SET(x)                 (((x) << GPIO_PIN11_SOURCE_LSB) & GPIO_PIN11_SOURCE_MASK)

#define GPIO_PIN12_ADDRESS                       0x00000058
#define GPIO_PIN12_OFFSET                        0x00000058
#define GPIO_PIN12_CONFIG_MSB                    12
#define GPIO_PIN12_CONFIG_LSB                    11
#define GPIO_PIN12_CONFIG_MASK                   0x00001800
#define GPIO_PIN12_CONFIG_GET(x)                 (((x) & GPIO_PIN12_CONFIG_MASK) >> GPIO_PIN12_CONFIG_LSB)
#define GPIO_PIN12_CONFIG_SET(x)                 (((x) << GPIO_PIN12_CONFIG_LSB) & GPIO_PIN12_CONFIG_MASK)
#define GPIO_PIN12_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN12_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN12_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN12_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN12_WAKEUP_ENABLE_MASK) >> GPIO_PIN12_WAKEUP_ENABLE_LSB)
#define GPIO_PIN12_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN12_WAKEUP_ENABLE_LSB) & GPIO_PIN12_WAKEUP_ENABLE_MASK)
#define GPIO_PIN12_INT_TYPE_MSB                  9
#define GPIO_PIN12_INT_TYPE_LSB                  7
#define GPIO_PIN12_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN12_INT_TYPE_GET(x)               (((x) & GPIO_PIN12_INT_TYPE_MASK) >> GPIO_PIN12_INT_TYPE_LSB)
#define GPIO_PIN12_INT_TYPE_SET(x)               (((x) << GPIO_PIN12_INT_TYPE_LSB) & GPIO_PIN12_INT_TYPE_MASK)
#define GPIO_PIN12_PAD_DRIVER_MSB                2
#define GPIO_PIN12_PAD_DRIVER_LSB                2
#define GPIO_PIN12_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN12_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN12_PAD_DRIVER_MASK) >> GPIO_PIN12_PAD_DRIVER_LSB)
#define GPIO_PIN12_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN12_PAD_DRIVER_LSB) & GPIO_PIN12_PAD_DRIVER_MASK)
#define GPIO_PIN12_SOURCE_MSB                    0
#define GPIO_PIN12_SOURCE_LSB                    0
#define GPIO_PIN12_SOURCE_MASK                   0x00000001
#define GPIO_PIN12_SOURCE_GET(x)                 (((x) & GPIO_PIN12_SOURCE_MASK) >> GPIO_PIN12_SOURCE_LSB)
#define GPIO_PIN12_SOURCE_SET(x)                 (((x) << GPIO_PIN12_SOURCE_LSB) & GPIO_PIN12_SOURCE_MASK)

#define GPIO_PIN13_ADDRESS                       0x0000005c
#define GPIO_PIN13_OFFSET                        0x0000005c
#define GPIO_PIN13_CONFIG_MSB                    12
#define GPIO_PIN13_CONFIG_LSB                    11
#define GPIO_PIN13_CONFIG_MASK                   0x00001800
#define GPIO_PIN13_CONFIG_GET(x)                 (((x) & GPIO_PIN13_CONFIG_MASK) >> GPIO_PIN13_CONFIG_LSB)
#define GPIO_PIN13_CONFIG_SET(x)                 (((x) << GPIO_PIN13_CONFIG_LSB) & GPIO_PIN13_CONFIG_MASK)
#define GPIO_PIN13_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN13_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN13_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN13_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN13_WAKEUP_ENABLE_MASK) >> GPIO_PIN13_WAKEUP_ENABLE_LSB)
#define GPIO_PIN13_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN13_WAKEUP_ENABLE_LSB) & GPIO_PIN13_WAKEUP_ENABLE_MASK)
#define GPIO_PIN13_INT_TYPE_MSB                  9
#define GPIO_PIN13_INT_TYPE_LSB                  7
#define GPIO_PIN13_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN13_INT_TYPE_GET(x)               (((x) & GPIO_PIN13_INT_TYPE_MASK) >> GPIO_PIN13_INT_TYPE_LSB)
#define GPIO_PIN13_INT_TYPE_SET(x)               (((x) << GPIO_PIN13_INT_TYPE_LSB) & GPIO_PIN13_INT_TYPE_MASK)
#define GPIO_PIN13_PAD_DRIVER_MSB                2
#define GPIO_PIN13_PAD_DRIVER_LSB                2
#define GPIO_PIN13_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN13_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN13_PAD_DRIVER_MASK) >> GPIO_PIN13_PAD_DRIVER_LSB)
#define GPIO_PIN13_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN13_PAD_DRIVER_LSB) & GPIO_PIN13_PAD_DRIVER_MASK)
#define GPIO_PIN13_SOURCE_MSB                    0
#define GPIO_PIN13_SOURCE_LSB                    0
#define GPIO_PIN13_SOURCE_MASK                   0x00000001
#define GPIO_PIN13_SOURCE_GET(x)                 (((x) & GPIO_PIN13_SOURCE_MASK) >> GPIO_PIN13_SOURCE_LSB)
#define GPIO_PIN13_SOURCE_SET(x)                 (((x) << GPIO_PIN13_SOURCE_LSB) & GPIO_PIN13_SOURCE_MASK)

#define GPIO_PIN14_ADDRESS                       0x00000060
#define GPIO_PIN14_OFFSET                        0x00000060
#define GPIO_PIN14_CONFIG_MSB                    12
#define GPIO_PIN14_CONFIG_LSB                    11
#define GPIO_PIN14_CONFIG_MASK                   0x00001800
#define GPIO_PIN14_CONFIG_GET(x)                 (((x) & GPIO_PIN14_CONFIG_MASK) >> GPIO_PIN14_CONFIG_LSB)
#define GPIO_PIN14_CONFIG_SET(x)                 (((x) << GPIO_PIN14_CONFIG_LSB) & GPIO_PIN14_CONFIG_MASK)
#define GPIO_PIN14_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN14_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN14_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN14_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN14_WAKEUP_ENABLE_MASK) >> GPIO_PIN14_WAKEUP_ENABLE_LSB)
#define GPIO_PIN14_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN14_WAKEUP_ENABLE_LSB) & GPIO_PIN14_WAKEUP_ENABLE_MASK)
#define GPIO_PIN14_INT_TYPE_MSB                  9
#define GPIO_PIN14_INT_TYPE_LSB                  7
#define GPIO_PIN14_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN14_INT_TYPE_GET(x)               (((x) & GPIO_PIN14_INT_TYPE_MASK) >> GPIO_PIN14_INT_TYPE_LSB)
#define GPIO_PIN14_INT_TYPE_SET(x)               (((x) << GPIO_PIN14_INT_TYPE_LSB) & GPIO_PIN14_INT_TYPE_MASK)
#define GPIO_PIN14_PAD_DRIVER_MSB                2
#define GPIO_PIN14_PAD_DRIVER_LSB                2
#define GPIO_PIN14_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN14_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN14_PAD_DRIVER_MASK) >> GPIO_PIN14_PAD_DRIVER_LSB)
#define GPIO_PIN14_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN14_PAD_DRIVER_LSB) & GPIO_PIN14_PAD_DRIVER_MASK)
#define GPIO_PIN14_SOURCE_MSB                    0
#define GPIO_PIN14_SOURCE_LSB                    0
#define GPIO_PIN14_SOURCE_MASK                   0x00000001
#define GPIO_PIN14_SOURCE_GET(x)                 (((x) & GPIO_PIN14_SOURCE_MASK) >> GPIO_PIN14_SOURCE_LSB)
#define GPIO_PIN14_SOURCE_SET(x)                 (((x) << GPIO_PIN14_SOURCE_LSB) & GPIO_PIN14_SOURCE_MASK)

#define GPIO_PIN15_ADDRESS                       0x00000064
#define GPIO_PIN15_OFFSET                        0x00000064
#define GPIO_PIN15_CONFIG_MSB                    12
#define GPIO_PIN15_CONFIG_LSB                    11
#define GPIO_PIN15_CONFIG_MASK                   0x00001800
#define GPIO_PIN15_CONFIG_GET(x)                 (((x) & GPIO_PIN15_CONFIG_MASK) >> GPIO_PIN15_CONFIG_LSB)
#define GPIO_PIN15_CONFIG_SET(x)                 (((x) << GPIO_PIN15_CONFIG_LSB) & GPIO_PIN15_CONFIG_MASK)
#define GPIO_PIN15_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN15_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN15_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN15_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN15_WAKEUP_ENABLE_MASK) >> GPIO_PIN15_WAKEUP_ENABLE_LSB)
#define GPIO_PIN15_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN15_WAKEUP_ENABLE_LSB) & GPIO_PIN15_WAKEUP_ENABLE_MASK)
#define GPIO_PIN15_INT_TYPE_MSB                  9
#define GPIO_PIN15_INT_TYPE_LSB                  7
#define GPIO_PIN15_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN15_INT_TYPE_GET(x)               (((x) & GPIO_PIN15_INT_TYPE_MASK) >> GPIO_PIN15_INT_TYPE_LSB)
#define GPIO_PIN15_INT_TYPE_SET(x)               (((x) << GPIO_PIN15_INT_TYPE_LSB) & GPIO_PIN15_INT_TYPE_MASK)
#define GPIO_PIN15_PAD_DRIVER_MSB                2
#define GPIO_PIN15_PAD_DRIVER_LSB                2
#define GPIO_PIN15_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN15_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN15_PAD_DRIVER_MASK) >> GPIO_PIN15_PAD_DRIVER_LSB)
#define GPIO_PIN15_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN15_PAD_DRIVER_LSB) & GPIO_PIN15_PAD_DRIVER_MASK)
#define GPIO_PIN15_SOURCE_MSB                    0
#define GPIO_PIN15_SOURCE_LSB                    0
#define GPIO_PIN15_SOURCE_MASK                   0x00000001
#define GPIO_PIN15_SOURCE_GET(x)                 (((x) & GPIO_PIN15_SOURCE_MASK) >> GPIO_PIN15_SOURCE_LSB)
#define GPIO_PIN15_SOURCE_SET(x)                 (((x) << GPIO_PIN15_SOURCE_LSB) & GPIO_PIN15_SOURCE_MASK)

#define GPIO_PIN16_ADDRESS                       0x00000068
#define GPIO_PIN16_OFFSET                        0x00000068
#define GPIO_PIN16_CONFIG_MSB                    12
#define GPIO_PIN16_CONFIG_LSB                    11
#define GPIO_PIN16_CONFIG_MASK                   0x00001800
#define GPIO_PIN16_CONFIG_GET(x)                 (((x) & GPIO_PIN16_CONFIG_MASK) >> GPIO_PIN16_CONFIG_LSB)
#define GPIO_PIN16_CONFIG_SET(x)                 (((x) << GPIO_PIN16_CONFIG_LSB) & GPIO_PIN16_CONFIG_MASK)
#define GPIO_PIN16_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN16_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN16_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN16_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN16_WAKEUP_ENABLE_MASK) >> GPIO_PIN16_WAKEUP_ENABLE_LSB)
#define GPIO_PIN16_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN16_WAKEUP_ENABLE_LSB) & GPIO_PIN16_WAKEUP_ENABLE_MASK)
#define GPIO_PIN16_INT_TYPE_MSB                  9
#define GPIO_PIN16_INT_TYPE_LSB                  7
#define GPIO_PIN16_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN16_INT_TYPE_GET(x)               (((x) & GPIO_PIN16_INT_TYPE_MASK) >> GPIO_PIN16_INT_TYPE_LSB)
#define GPIO_PIN16_INT_TYPE_SET(x)               (((x) << GPIO_PIN16_INT_TYPE_LSB) & GPIO_PIN16_INT_TYPE_MASK)
#define GPIO_PIN16_PAD_DRIVER_MSB                2
#define GPIO_PIN16_PAD_DRIVER_LSB                2
#define GPIO_PIN16_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN16_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN16_PAD_DRIVER_MASK) >> GPIO_PIN16_PAD_DRIVER_LSB)
#define GPIO_PIN16_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN16_PAD_DRIVER_LSB) & GPIO_PIN16_PAD_DRIVER_MASK)
#define GPIO_PIN16_SOURCE_MSB                    0
#define GPIO_PIN16_SOURCE_LSB                    0
#define GPIO_PIN16_SOURCE_MASK                   0x00000001
#define GPIO_PIN16_SOURCE_GET(x)                 (((x) & GPIO_PIN16_SOURCE_MASK) >> GPIO_PIN16_SOURCE_LSB)
#define GPIO_PIN16_SOURCE_SET(x)                 (((x) << GPIO_PIN16_SOURCE_LSB) & GPIO_PIN16_SOURCE_MASK)

#define GPIO_PIN17_ADDRESS                       0x0000006c
#define GPIO_PIN17_OFFSET                        0x0000006c
#define GPIO_PIN17_CONFIG_MSB                    12
#define GPIO_PIN17_CONFIG_LSB                    11
#define GPIO_PIN17_CONFIG_MASK                   0x00001800
#define GPIO_PIN17_CONFIG_GET(x)                 (((x) & GPIO_PIN17_CONFIG_MASK) >> GPIO_PIN17_CONFIG_LSB)
#define GPIO_PIN17_CONFIG_SET(x)                 (((x) << GPIO_PIN17_CONFIG_LSB) & GPIO_PIN17_CONFIG_MASK)
#define GPIO_PIN17_WAKEUP_ENABLE_MSB             10
#define GPIO_PIN17_WAKEUP_ENABLE_LSB             10
#define GPIO_PIN17_WAKEUP_ENABLE_MASK            0x00000400
#define GPIO_PIN17_WAKEUP_ENABLE_GET(x)          (((x) & GPIO_PIN17_WAKEUP_ENABLE_MASK) >> GPIO_PIN17_WAKEUP_ENABLE_LSB)
#define GPIO_PIN17_WAKEUP_ENABLE_SET(x)          (((x) << GPIO_PIN17_WAKEUP_ENABLE_LSB) & GPIO_PIN17_WAKEUP_ENABLE_MASK)
#define GPIO_PIN17_INT_TYPE_MSB                  9
#define GPIO_PIN17_INT_TYPE_LSB                  7
#define GPIO_PIN17_INT_TYPE_MASK                 0x00000380
#define GPIO_PIN17_INT_TYPE_GET(x)               (((x) & GPIO_PIN17_INT_TYPE_MASK) >> GPIO_PIN17_INT_TYPE_LSB)
#define GPIO_PIN17_INT_TYPE_SET(x)               (((x) << GPIO_PIN17_INT_TYPE_LSB) & GPIO_PIN17_INT_TYPE_MASK)
#define GPIO_PIN17_PAD_DRIVER_MSB                2
#define GPIO_PIN17_PAD_DRIVER_LSB                2
#define GPIO_PIN17_PAD_DRIVER_MASK               0x00000004
#define GPIO_PIN17_PAD_DRIVER_GET(x)             (((x) & GPIO_PIN17_PAD_DRIVER_MASK) >> GPIO_PIN17_PAD_DRIVER_LSB)
#define GPIO_PIN17_PAD_DRIVER_SET(x)             (((x) << GPIO_PIN17_PAD_DRIVER_LSB) & GPIO_PIN17_PAD_DRIVER_MASK)
#define GPIO_PIN17_SOURCE_MSB                    0
#define GPIO_PIN17_SOURCE_LSB                    0
#define GPIO_PIN17_SOURCE_MASK                   0x00000001
#define GPIO_PIN17_SOURCE_GET(x)                 (((x) & GPIO_PIN17_SOURCE_MASK) >> GPIO_PIN17_SOURCE_LSB)
#define GPIO_PIN17_SOURCE_SET(x)                 (((x) << GPIO_PIN17_SOURCE_LSB) & GPIO_PIN17_SOURCE_MASK)

#define SDIO_PIN_ADDRESS                         0x00000070
#define SDIO_PIN_OFFSET                          0x00000070
#define SDIO_PIN_PAD_PULL_MSB                    3
#define SDIO_PIN_PAD_PULL_LSB                    2
#define SDIO_PIN_PAD_PULL_MASK                   0x0000000c
#define SDIO_PIN_PAD_PULL_GET(x)                 (((x) & SDIO_PIN_PAD_PULL_MASK) >> SDIO_PIN_PAD_PULL_LSB)
#define SDIO_PIN_PAD_PULL_SET(x)                 (((x) << SDIO_PIN_PAD_PULL_LSB) & SDIO_PIN_PAD_PULL_MASK)
#define SDIO_PIN_PAD_STRENGTH_MSB                1
#define SDIO_PIN_PAD_STRENGTH_LSB                0
#define SDIO_PIN_PAD_STRENGTH_MASK               0x00000003
#define SDIO_PIN_PAD_STRENGTH_GET(x)             (((x) & SDIO_PIN_PAD_STRENGTH_MASK) >> SDIO_PIN_PAD_STRENGTH_LSB)
#define SDIO_PIN_PAD_STRENGTH_SET(x)             (((x) << SDIO_PIN_PAD_STRENGTH_LSB) & SDIO_PIN_PAD_STRENGTH_MASK)

#define CLK_REQ_PIN_ADDRESS                      0x00000074
#define CLK_REQ_PIN_OFFSET                       0x00000074
#define CLK_REQ_PIN_ATE_OE_L_MSB                 4
#define CLK_REQ_PIN_ATE_OE_L_LSB                 4
#define CLK_REQ_PIN_ATE_OE_L_MASK                0x00000010
#define CLK_REQ_PIN_ATE_OE_L_GET(x)              (((x) & CLK_REQ_PIN_ATE_OE_L_MASK) >> CLK_REQ_PIN_ATE_OE_L_LSB)
#define CLK_REQ_PIN_ATE_OE_L_SET(x)              (((x) << CLK_REQ_PIN_ATE_OE_L_LSB) & CLK_REQ_PIN_ATE_OE_L_MASK)
#define CLK_REQ_PIN_PAD_PULL_MSB                 3
#define CLK_REQ_PIN_PAD_PULL_LSB                 2
#define CLK_REQ_PIN_PAD_PULL_MASK                0x0000000c
#define CLK_REQ_PIN_PAD_PULL_GET(x)              (((x) & CLK_REQ_PIN_PAD_PULL_MASK) >> CLK_REQ_PIN_PAD_PULL_LSB)
#define CLK_REQ_PIN_PAD_PULL_SET(x)              (((x) << CLK_REQ_PIN_PAD_PULL_LSB) & CLK_REQ_PIN_PAD_PULL_MASK)
#define CLK_REQ_PIN_PAD_STRENGTH_MSB             1
#define CLK_REQ_PIN_PAD_STRENGTH_LSB             0
#define CLK_REQ_PIN_PAD_STRENGTH_MASK            0x00000003
#define CLK_REQ_PIN_PAD_STRENGTH_GET(x)          (((x) & CLK_REQ_PIN_PAD_STRENGTH_MASK) >> CLK_REQ_PIN_PAD_STRENGTH_LSB)
#define CLK_REQ_PIN_PAD_STRENGTH_SET(x)          (((x) << CLK_REQ_PIN_PAD_STRENGTH_LSB) & CLK_REQ_PIN_PAD_STRENGTH_MASK)

#define SIGMA_DELTA_ADDRESS                      0x00000078
#define SIGMA_DELTA_OFFSET                       0x00000078
#define SIGMA_DELTA_ENABLE_MSB                   16
#define SIGMA_DELTA_ENABLE_LSB                   16
#define SIGMA_DELTA_ENABLE_MASK                  0x00010000
#define SIGMA_DELTA_ENABLE_GET(x)                (((x) & SIGMA_DELTA_ENABLE_MASK) >> SIGMA_DELTA_ENABLE_LSB)
#define SIGMA_DELTA_ENABLE_SET(x)                (((x) << SIGMA_DELTA_ENABLE_LSB) & SIGMA_DELTA_ENABLE_MASK)
#define SIGMA_DELTA_PRESCALAR_MSB                15
#define SIGMA_DELTA_PRESCALAR_LSB                8
#define SIGMA_DELTA_PRESCALAR_MASK               0x0000ff00
#define SIGMA_DELTA_PRESCALAR_GET(x)             (((x) & SIGMA_DELTA_PRESCALAR_MASK) >> SIGMA_DELTA_PRESCALAR_LSB)
#define SIGMA_DELTA_PRESCALAR_SET(x)             (((x) << SIGMA_DELTA_PRESCALAR_LSB) & SIGMA_DELTA_PRESCALAR_MASK)
#define SIGMA_DELTA_TARGET_MSB                   7
#define SIGMA_DELTA_TARGET_LSB                   0
#define SIGMA_DELTA_TARGET_MASK                  0x000000ff
#define SIGMA_DELTA_TARGET_GET(x)                (((x) & SIGMA_DELTA_TARGET_MASK) >> SIGMA_DELTA_TARGET_LSB)
#define SIGMA_DELTA_TARGET_SET(x)                (((x) << SIGMA_DELTA_TARGET_LSB) & SIGMA_DELTA_TARGET_MASK)

#define DEBUG_CONTROL_ADDRESS                    0x0000007c
#define DEBUG_CONTROL_OFFSET                     0x0000007c
#define DEBUG_CONTROL_OBS_OE_L_MSB               1
#define DEBUG_CONTROL_OBS_OE_L_LSB               1
#define DEBUG_CONTROL_OBS_OE_L_MASK              0x00000002
#define DEBUG_CONTROL_OBS_OE_L_GET(x)            (((x) & DEBUG_CONTROL_OBS_OE_L_MASK) >> DEBUG_CONTROL_OBS_OE_L_LSB)
#define DEBUG_CONTROL_OBS_OE_L_SET(x)            (((x) << DEBUG_CONTROL_OBS_OE_L_LSB) & DEBUG_CONTROL_OBS_OE_L_MASK)
#define DEBUG_CONTROL_ENABLE_MSB                 0
#define DEBUG_CONTROL_ENABLE_LSB                 0
#define DEBUG_CONTROL_ENABLE_MASK                0x00000001
#define DEBUG_CONTROL_ENABLE_GET(x)              (((x) & DEBUG_CONTROL_ENABLE_MASK) >> DEBUG_CONTROL_ENABLE_LSB)
#define DEBUG_CONTROL_ENABLE_SET(x)              (((x) << DEBUG_CONTROL_ENABLE_LSB) & DEBUG_CONTROL_ENABLE_MASK)

#define DEBUG_INPUT_SEL_ADDRESS                  0x00000080
#define DEBUG_INPUT_SEL_OFFSET                   0x00000080
#define DEBUG_INPUT_SEL_SRC_MSB                  3
#define DEBUG_INPUT_SEL_SRC_LSB                  0
#define DEBUG_INPUT_SEL_SRC_MASK                 0x0000000f
#define DEBUG_INPUT_SEL_SRC_GET(x)               (((x) & DEBUG_INPUT_SEL_SRC_MASK) >> DEBUG_INPUT_SEL_SRC_LSB)
#define DEBUG_INPUT_SEL_SRC_SET(x)               (((x) << DEBUG_INPUT_SEL_SRC_LSB) & DEBUG_INPUT_SEL_SRC_MASK)

#define DEBUG_OUT_ADDRESS                        0x00000084
#define DEBUG_OUT_OFFSET                         0x00000084
#define DEBUG_OUT_DATA_MSB                       17
#define DEBUG_OUT_DATA_LSB                       0
#define DEBUG_OUT_DATA_MASK                      0x0003ffff
#define DEBUG_OUT_DATA_GET(x)                    (((x) & DEBUG_OUT_DATA_MASK) >> DEBUG_OUT_DATA_LSB)
#define DEBUG_OUT_DATA_SET(x)                    (((x) << DEBUG_OUT_DATA_LSB) & DEBUG_OUT_DATA_MASK)

#define LA_CONTROL_ADDRESS                       0x00000088
#define LA_CONTROL_OFFSET                        0x00000088
#define LA_CONTROL_RUN_MSB                       1
#define LA_CONTROL_RUN_LSB                       1
#define LA_CONTROL_RUN_MASK                      0x00000002
#define LA_CONTROL_RUN_GET(x)                    (((x) & LA_CONTROL_RUN_MASK) >> LA_CONTROL_RUN_LSB)
#define LA_CONTROL_RUN_SET(x)                    (((x) << LA_CONTROL_RUN_LSB) & LA_CONTROL_RUN_MASK)
#define LA_CONTROL_TRIGGERED_MSB                 0
#define LA_CONTROL_TRIGGERED_LSB                 0
#define LA_CONTROL_TRIGGERED_MASK                0x00000001
#define LA_CONTROL_TRIGGERED_GET(x)              (((x) & LA_CONTROL_TRIGGERED_MASK) >> LA_CONTROL_TRIGGERED_LSB)
#define LA_CONTROL_TRIGGERED_SET(x)              (((x) << LA_CONTROL_TRIGGERED_LSB) & LA_CONTROL_TRIGGERED_MASK)

#define LA_CLOCK_ADDRESS                         0x0000008c
#define LA_CLOCK_OFFSET                          0x0000008c
#define LA_CLOCK_DIV_MSB                         7
#define LA_CLOCK_DIV_LSB                         0
#define LA_CLOCK_DIV_MASK                        0x000000ff
#define LA_CLOCK_DIV_GET(x)                      (((x) & LA_CLOCK_DIV_MASK) >> LA_CLOCK_DIV_LSB)
#define LA_CLOCK_DIV_SET(x)                      (((x) << LA_CLOCK_DIV_LSB) & LA_CLOCK_DIV_MASK)

#define LA_STATUS_ADDRESS                        0x00000090
#define LA_STATUS_OFFSET                         0x00000090
#define LA_STATUS_INTERRUPT_MSB                  0
#define LA_STATUS_INTERRUPT_LSB                  0
#define LA_STATUS_INTERRUPT_MASK                 0x00000001
#define LA_STATUS_INTERRUPT_GET(x)               (((x) & LA_STATUS_INTERRUPT_MASK) >> LA_STATUS_INTERRUPT_LSB)
#define LA_STATUS_INTERRUPT_SET(x)               (((x) << LA_STATUS_INTERRUPT_LSB) & LA_STATUS_INTERRUPT_MASK)

#define LA_TRIGGER_SAMPLE_ADDRESS                0x00000094
#define LA_TRIGGER_SAMPLE_OFFSET                 0x00000094
#define LA_TRIGGER_SAMPLE_COUNT_MSB              15
#define LA_TRIGGER_SAMPLE_COUNT_LSB              0
#define LA_TRIGGER_SAMPLE_COUNT_MASK             0x0000ffff
#define LA_TRIGGER_SAMPLE_COUNT_GET(x)           (((x) & LA_TRIGGER_SAMPLE_COUNT_MASK) >> LA_TRIGGER_SAMPLE_COUNT_LSB)
#define LA_TRIGGER_SAMPLE_COUNT_SET(x)           (((x) << LA_TRIGGER_SAMPLE_COUNT_LSB) & LA_TRIGGER_SAMPLE_COUNT_MASK)

#define LA_TRIGGER_POSITION_ADDRESS              0x00000098
#define LA_TRIGGER_POSITION_OFFSET               0x00000098
#define LA_TRIGGER_POSITION_VALUE_MSB            15
#define LA_TRIGGER_POSITION_VALUE_LSB            0
#define LA_TRIGGER_POSITION_VALUE_MASK           0x0000ffff
#define LA_TRIGGER_POSITION_VALUE_GET(x)         (((x) & LA_TRIGGER_POSITION_VALUE_MASK) >> LA_TRIGGER_POSITION_VALUE_LSB)
#define LA_TRIGGER_POSITION_VALUE_SET(x)         (((x) << LA_TRIGGER_POSITION_VALUE_LSB) & LA_TRIGGER_POSITION_VALUE_MASK)

#define LA_PRE_TRIGGER_ADDRESS                   0x0000009c
#define LA_PRE_TRIGGER_OFFSET                    0x0000009c
#define LA_PRE_TRIGGER_COUNT_MSB                 15
#define LA_PRE_TRIGGER_COUNT_LSB                 0
#define LA_PRE_TRIGGER_COUNT_MASK                0x0000ffff
#define LA_PRE_TRIGGER_COUNT_GET(x)              (((x) & LA_PRE_TRIGGER_COUNT_MASK) >> LA_PRE_TRIGGER_COUNT_LSB)
#define LA_PRE_TRIGGER_COUNT_SET(x)              (((x) << LA_PRE_TRIGGER_COUNT_LSB) & LA_PRE_TRIGGER_COUNT_MASK)

#define LA_POST_TRIGGER_ADDRESS                  0x000000a0
#define LA_POST_TRIGGER_OFFSET                   0x000000a0
#define LA_POST_TRIGGER_COUNT_MSB                15
#define LA_POST_TRIGGER_COUNT_LSB                0
#define LA_POST_TRIGGER_COUNT_MASK               0x0000ffff
#define LA_POST_TRIGGER_COUNT_GET(x)             (((x) & LA_POST_TRIGGER_COUNT_MASK) >> LA_POST_TRIGGER_COUNT_LSB)
#define LA_POST_TRIGGER_COUNT_SET(x)             (((x) << LA_POST_TRIGGER_COUNT_LSB) & LA_POST_TRIGGER_COUNT_MASK)

#define LA_FILTER_CONTROL_ADDRESS                0x000000a4
#define LA_FILTER_CONTROL_OFFSET                 0x000000a4
#define LA_FILTER_CONTROL_DELTA_MSB              0
#define LA_FILTER_CONTROL_DELTA_LSB              0
#define LA_FILTER_CONTROL_DELTA_MASK             0x00000001
#define LA_FILTER_CONTROL_DELTA_GET(x)           (((x) & LA_FILTER_CONTROL_DELTA_MASK) >> LA_FILTER_CONTROL_DELTA_LSB)
#define LA_FILTER_CONTROL_DELTA_SET(x)           (((x) << LA_FILTER_CONTROL_DELTA_LSB) & LA_FILTER_CONTROL_DELTA_MASK)

#define LA_FILTER_DATA_ADDRESS                   0x000000a8
#define LA_FILTER_DATA_OFFSET                    0x000000a8
#define LA_FILTER_DATA_MATCH_MSB                 17
#define LA_FILTER_DATA_MATCH_LSB                 0
#define LA_FILTER_DATA_MATCH_MASK                0x0003ffff
#define LA_FILTER_DATA_MATCH_GET(x)              (((x) & LA_FILTER_DATA_MATCH_MASK) >> LA_FILTER_DATA_MATCH_LSB)
#define LA_FILTER_DATA_MATCH_SET(x)              (((x) << LA_FILTER_DATA_MATCH_LSB) & LA_FILTER_DATA_MATCH_MASK)

#define LA_FILTER_WILDCARD_ADDRESS               0x000000ac
#define LA_FILTER_WILDCARD_OFFSET                0x000000ac
#define LA_FILTER_WILDCARD_MATCH_MSB             17
#define LA_FILTER_WILDCARD_MATCH_LSB             0
#define LA_FILTER_WILDCARD_MATCH_MASK            0x0003ffff
#define LA_FILTER_WILDCARD_MATCH_GET(x)          (((x) & LA_FILTER_WILDCARD_MATCH_MASK) >> LA_FILTER_WILDCARD_MATCH_LSB)
#define LA_FILTER_WILDCARD_MATCH_SET(x)          (((x) << LA_FILTER_WILDCARD_MATCH_LSB) & LA_FILTER_WILDCARD_MATCH_MASK)

#define LA_TRIGGERA_DATA_ADDRESS                 0x000000b0
#define LA_TRIGGERA_DATA_OFFSET                  0x000000b0
#define LA_TRIGGERA_DATA_MATCH_MSB               17
#define LA_TRIGGERA_DATA_MATCH_LSB               0
#define LA_TRIGGERA_DATA_MATCH_MASK              0x0003ffff
#define LA_TRIGGERA_DATA_MATCH_GET(x)            (((x) & LA_TRIGGERA_DATA_MATCH_MASK) >> LA_TRIGGERA_DATA_MATCH_LSB)
#define LA_TRIGGERA_DATA_MATCH_SET(x)            (((x) << LA_TRIGGERA_DATA_MATCH_LSB) & LA_TRIGGERA_DATA_MATCH_MASK)

#define LA_TRIGGERA_WILDCARD_ADDRESS             0x000000b4
#define LA_TRIGGERA_WILDCARD_OFFSET              0x000000b4
#define LA_TRIGGERA_WILDCARD_MATCH_MSB           17
#define LA_TRIGGERA_WILDCARD_MATCH_LSB           0
#define LA_TRIGGERA_WILDCARD_MATCH_MASK          0x0003ffff
#define LA_TRIGGERA_WILDCARD_MATCH_GET(x)        (((x) & LA_TRIGGERA_WILDCARD_MATCH_MASK) >> LA_TRIGGERA_WILDCARD_MATCH_LSB)
#define LA_TRIGGERA_WILDCARD_MATCH_SET(x)        (((x) << LA_TRIGGERA_WILDCARD_MATCH_LSB) & LA_TRIGGERA_WILDCARD_MATCH_MASK)

#define LA_TRIGGERB_DATA_ADDRESS                 0x000000b8
#define LA_TRIGGERB_DATA_OFFSET                  0x000000b8
#define LA_TRIGGERB_DATA_MATCH_MSB               17
#define LA_TRIGGERB_DATA_MATCH_LSB               0
#define LA_TRIGGERB_DATA_MATCH_MASK              0x0003ffff
#define LA_TRIGGERB_DATA_MATCH_GET(x)            (((x) & LA_TRIGGERB_DATA_MATCH_MASK) >> LA_TRIGGERB_DATA_MATCH_LSB)
#define LA_TRIGGERB_DATA_MATCH_SET(x)            (((x) << LA_TRIGGERB_DATA_MATCH_LSB) & LA_TRIGGERB_DATA_MATCH_MASK)

#define LA_TRIGGERB_WILDCARD_ADDRESS             0x000000bc
#define LA_TRIGGERB_WILDCARD_OFFSET              0x000000bc
#define LA_TRIGGERB_WILDCARD_MATCH_MSB           17
#define LA_TRIGGERB_WILDCARD_MATCH_LSB           0
#define LA_TRIGGERB_WILDCARD_MATCH_MASK          0x0003ffff
#define LA_TRIGGERB_WILDCARD_MATCH_GET(x)        (((x) & LA_TRIGGERB_WILDCARD_MATCH_MASK) >> LA_TRIGGERB_WILDCARD_MATCH_LSB)
#define LA_TRIGGERB_WILDCARD_MATCH_SET(x)        (((x) << LA_TRIGGERB_WILDCARD_MATCH_LSB) & LA_TRIGGERB_WILDCARD_MATCH_MASK)

#define LA_TRIGGER_ADDRESS                       0x000000c0
#define LA_TRIGGER_OFFSET                        0x000000c0
#define LA_TRIGGER_EVENT_MSB                     2
#define LA_TRIGGER_EVENT_LSB                     0
#define LA_TRIGGER_EVENT_MASK                    0x00000007
#define LA_TRIGGER_EVENT_GET(x)                  (((x) & LA_TRIGGER_EVENT_MASK) >> LA_TRIGGER_EVENT_LSB)
#define LA_TRIGGER_EVENT_SET(x)                  (((x) << LA_TRIGGER_EVENT_LSB) & LA_TRIGGER_EVENT_MASK)

#define LA_FIFO_ADDRESS                          0x000000c4
#define LA_FIFO_OFFSET                           0x000000c4
#define LA_FIFO_FULL_MSB                         1
#define LA_FIFO_FULL_LSB                         1
#define LA_FIFO_FULL_MASK                        0x00000002
#define LA_FIFO_FULL_GET(x)                      (((x) & LA_FIFO_FULL_MASK) >> LA_FIFO_FULL_LSB)
#define LA_FIFO_FULL_SET(x)                      (((x) << LA_FIFO_FULL_LSB) & LA_FIFO_FULL_MASK)
#define LA_FIFO_EMPTY_MSB                        0
#define LA_FIFO_EMPTY_LSB                        0
#define LA_FIFO_EMPTY_MASK                       0x00000001
#define LA_FIFO_EMPTY_GET(x)                     (((x) & LA_FIFO_EMPTY_MASK) >> LA_FIFO_EMPTY_LSB)
#define LA_FIFO_EMPTY_SET(x)                     (((x) << LA_FIFO_EMPTY_LSB) & LA_FIFO_EMPTY_MASK)

#define LA_ADDRESS                               0x000000c8
#define LA_OFFSET                                0x000000c8
#define LA_DATA_MSB                              17
#define LA_DATA_LSB                              0
#define LA_DATA_MASK                             0x0003ffff
#define LA_DATA_GET(x)                           (((x) & LA_DATA_MASK) >> LA_DATA_LSB)
#define LA_DATA_SET(x)                           (((x) << LA_DATA_LSB) & LA_DATA_MASK)

#define ANT_PIN_ADDRESS                          0x000000d0
#define ANT_PIN_OFFSET                           0x000000d0
#define ANT_PIN_PAD_PULL_MSB                     3
#define ANT_PIN_PAD_PULL_LSB                     2
#define ANT_PIN_PAD_PULL_MASK                    0x0000000c
#define ANT_PIN_PAD_PULL_GET(x)                  (((x) & ANT_PIN_PAD_PULL_MASK) >> ANT_PIN_PAD_PULL_LSB)
#define ANT_PIN_PAD_PULL_SET(x)                  (((x) << ANT_PIN_PAD_PULL_LSB) & ANT_PIN_PAD_PULL_MASK)
#define ANT_PIN_PAD_STRENGTH_MSB                 1
#define ANT_PIN_PAD_STRENGTH_LSB                 0
#define ANT_PIN_PAD_STRENGTH_MASK                0x00000003
#define ANT_PIN_PAD_STRENGTH_GET(x)              (((x) & ANT_PIN_PAD_STRENGTH_MASK) >> ANT_PIN_PAD_STRENGTH_LSB)
#define ANT_PIN_PAD_STRENGTH_SET(x)              (((x) << ANT_PIN_PAD_STRENGTH_LSB) & ANT_PIN_PAD_STRENGTH_MASK)

#define ANTD_PIN_ADDRESS                         0x000000d4
#define ANTD_PIN_OFFSET                          0x000000d4
#define ANTD_PIN_PAD_PULL_MSB                    1
#define ANTD_PIN_PAD_PULL_LSB                    0
#define ANTD_PIN_PAD_PULL_MASK                   0x00000003
#define ANTD_PIN_PAD_PULL_GET(x)                 (((x) & ANTD_PIN_PAD_PULL_MASK) >> ANTD_PIN_PAD_PULL_LSB)
#define ANTD_PIN_PAD_PULL_SET(x)                 (((x) << ANTD_PIN_PAD_PULL_LSB) & ANTD_PIN_PAD_PULL_MASK)

#define GPIO_PIN_ADDRESS                         0x000000d8
#define GPIO_PIN_OFFSET                          0x000000d8
#define GPIO_PIN_PAD_PULL_MSB                    3
#define GPIO_PIN_PAD_PULL_LSB                    2
#define GPIO_PIN_PAD_PULL_MASK                   0x0000000c
#define GPIO_PIN_PAD_PULL_GET(x)                 (((x) & GPIO_PIN_PAD_PULL_MASK) >> GPIO_PIN_PAD_PULL_LSB)
#define GPIO_PIN_PAD_PULL_SET(x)                 (((x) << GPIO_PIN_PAD_PULL_LSB) & GPIO_PIN_PAD_PULL_MASK)
#define GPIO_PIN_PAD_STRENGTH_MSB                1
#define GPIO_PIN_PAD_STRENGTH_LSB                0
#define GPIO_PIN_PAD_STRENGTH_MASK               0x00000003
#define GPIO_PIN_PAD_STRENGTH_GET(x)             (((x) & GPIO_PIN_PAD_STRENGTH_MASK) >> GPIO_PIN_PAD_STRENGTH_LSB)
#define GPIO_PIN_PAD_STRENGTH_SET(x)             (((x) << GPIO_PIN_PAD_STRENGTH_LSB) & GPIO_PIN_PAD_STRENGTH_MASK)

#define GPIO_H_PIN_ADDRESS                       0x000000dc
#define GPIO_H_PIN_OFFSET                        0x000000dc
#define GPIO_H_PIN_PAD_PULL_MSB                  1
#define GPIO_H_PIN_PAD_PULL_LSB                  0
#define GPIO_H_PIN_PAD_PULL_MASK                 0x00000003
#define GPIO_H_PIN_PAD_PULL_GET(x)               (((x) & GPIO_H_PIN_PAD_PULL_MASK) >> GPIO_H_PIN_PAD_PULL_LSB)
#define GPIO_H_PIN_PAD_PULL_SET(x)               (((x) << GPIO_H_PIN_PAD_PULL_LSB) & GPIO_H_PIN_PAD_PULL_MASK)

#define BT_PIN_ADDRESS                           0x000000e0
#define BT_PIN_OFFSET                            0x000000e0
#define BT_PIN_PAD_PULL_MSB                      3
#define BT_PIN_PAD_PULL_LSB                      2
#define BT_PIN_PAD_PULL_MASK                     0x0000000c
#define BT_PIN_PAD_PULL_GET(x)                   (((x) & BT_PIN_PAD_PULL_MASK) >> BT_PIN_PAD_PULL_LSB)
#define BT_PIN_PAD_PULL_SET(x)                   (((x) << BT_PIN_PAD_PULL_LSB) & BT_PIN_PAD_PULL_MASK)
#define BT_PIN_PAD_STRENGTH_MSB                  1
#define BT_PIN_PAD_STRENGTH_LSB                  0
#define BT_PIN_PAD_STRENGTH_MASK                 0x00000003
#define BT_PIN_PAD_STRENGTH_GET(x)               (((x) & BT_PIN_PAD_STRENGTH_MASK) >> BT_PIN_PAD_STRENGTH_LSB)
#define BT_PIN_PAD_STRENGTH_SET(x)               (((x) << BT_PIN_PAD_STRENGTH_LSB) & BT_PIN_PAD_STRENGTH_MASK)

#define BT_WLAN_PIN_ADDRESS                      0x000000e4
#define BT_WLAN_PIN_OFFSET                       0x000000e4
#define BT_WLAN_PIN_PAD_PULL_MSB                 1
#define BT_WLAN_PIN_PAD_PULL_LSB                 0
#define BT_WLAN_PIN_PAD_PULL_MASK                0x00000003
#define BT_WLAN_PIN_PAD_PULL_GET(x)              (((x) & BT_WLAN_PIN_PAD_PULL_MASK) >> BT_WLAN_PIN_PAD_PULL_LSB)
#define BT_WLAN_PIN_PAD_PULL_SET(x)              (((x) << BT_WLAN_PIN_PAD_PULL_LSB) & BT_WLAN_PIN_PAD_PULL_MASK)

#define SI_UART_PIN_ADDRESS                      0x000000e8
#define SI_UART_PIN_OFFSET                       0x000000e8
#define SI_UART_PIN_PAD_PULL_MSB                 3
#define SI_UART_PIN_PAD_PULL_LSB                 2
#define SI_UART_PIN_PAD_PULL_MASK                0x0000000c
#define SI_UART_PIN_PAD_PULL_GET(x)              (((x) & SI_UART_PIN_PAD_PULL_MASK) >> SI_UART_PIN_PAD_PULL_LSB)
#define SI_UART_PIN_PAD_PULL_SET(x)              (((x) << SI_UART_PIN_PAD_PULL_LSB) & SI_UART_PIN_PAD_PULL_MASK)
#define SI_UART_PIN_PAD_STRENGTH_MSB             1
#define SI_UART_PIN_PAD_STRENGTH_LSB             0
#define SI_UART_PIN_PAD_STRENGTH_MASK            0x00000003
#define SI_UART_PIN_PAD_STRENGTH_GET(x)          (((x) & SI_UART_PIN_PAD_STRENGTH_MASK) >> SI_UART_PIN_PAD_STRENGTH_LSB)
#define SI_UART_PIN_PAD_STRENGTH_SET(x)          (((x) << SI_UART_PIN_PAD_STRENGTH_LSB) & SI_UART_PIN_PAD_STRENGTH_MASK)

#define CLK32K_PIN_ADDRESS                       0x000000ec
#define CLK32K_PIN_OFFSET                        0x000000ec
#define CLK32K_PIN_PAD_PULL_MSB                  1
#define CLK32K_PIN_PAD_PULL_LSB                  0
#define CLK32K_PIN_PAD_PULL_MASK                 0x00000003
#define CLK32K_PIN_PAD_PULL_GET(x)               (((x) & CLK32K_PIN_PAD_PULL_MASK) >> CLK32K_PIN_PAD_PULL_LSB)
#define CLK32K_PIN_PAD_PULL_SET(x)               (((x) << CLK32K_PIN_PAD_PULL_LSB) & CLK32K_PIN_PAD_PULL_MASK)

#define RESET_TUPLE_STATUS_ADDRESS               0x000000f0
#define RESET_TUPLE_STATUS_OFFSET                0x000000f0
#define RESET_TUPLE_STATUS_TEST_RESET_TUPLE_MSB  11
#define RESET_TUPLE_STATUS_TEST_RESET_TUPLE_LSB  8
#define RESET_TUPLE_STATUS_TEST_RESET_TUPLE_MASK 0x00000f00
#define RESET_TUPLE_STATUS_TEST_RESET_TUPLE_GET(x) (((x) & RESET_TUPLE_STATUS_TEST_RESET_TUPLE_MASK) >> RESET_TUPLE_STATUS_TEST_RESET_TUPLE_LSB)
#define RESET_TUPLE_STATUS_TEST_RESET_TUPLE_SET(x) (((x) << RESET_TUPLE_STATUS_TEST_RESET_TUPLE_LSB) & RESET_TUPLE_STATUS_TEST_RESET_TUPLE_MASK)
#define RESET_TUPLE_STATUS_PIN_RESET_TUPLE_MSB   7
#define RESET_TUPLE_STATUS_PIN_RESET_TUPLE_LSB   0
#define RESET_TUPLE_STATUS_PIN_RESET_TUPLE_MASK  0x000000ff
#define RESET_TUPLE_STATUS_PIN_RESET_TUPLE_GET(x) (((x) & RESET_TUPLE_STATUS_PIN_RESET_TUPLE_MASK) >> RESET_TUPLE_STATUS_PIN_RESET_TUPLE_LSB)
#define RESET_TUPLE_STATUS_PIN_RESET_TUPLE_SET(x) (((x) << RESET_TUPLE_STATUS_PIN_RESET_TUPLE_LSB) & RESET_TUPLE_STATUS_PIN_RESET_TUPLE_MASK)


#ifndef __ASSEMBLER__

typedef struct gpio_reg_reg_s {
  volatile unsigned int gpio_out;
  volatile unsigned int gpio_out_w1ts;
  volatile unsigned int gpio_out_w1tc;
  volatile unsigned int gpio_enable;
  volatile unsigned int gpio_enable_w1ts;
  volatile unsigned int gpio_enable_w1tc;
  volatile unsigned int gpio_in;
  volatile unsigned int gpio_status;
  volatile unsigned int gpio_status_w1ts;
  volatile unsigned int gpio_status_w1tc;
  volatile unsigned int gpio_pin0;
  volatile unsigned int gpio_pin1;
  volatile unsigned int gpio_pin2;
  volatile unsigned int gpio_pin3;
  volatile unsigned int gpio_pin4;
  volatile unsigned int gpio_pin5;
  volatile unsigned int gpio_pin6;
  volatile unsigned int gpio_pin7;
  volatile unsigned int gpio_pin8;
  volatile unsigned int gpio_pin9;
  volatile unsigned int gpio_pin10;
  volatile unsigned int gpio_pin11;
  volatile unsigned int gpio_pin12;
  volatile unsigned int gpio_pin13;
  volatile unsigned int gpio_pin14;
  volatile unsigned int gpio_pin15;
  volatile unsigned int gpio_pin16;
  volatile unsigned int gpio_pin17;
  volatile unsigned int sdio_pin;
  volatile unsigned int clk_req_pin;
  volatile unsigned int sigma_delta;
  volatile unsigned int debug_control;
  volatile unsigned int debug_input_sel;
  volatile unsigned int debug_out;
  volatile unsigned int la_control;
  volatile unsigned int la_clock;
  volatile unsigned int la_status;
  volatile unsigned int la_trigger_sample;
  volatile unsigned int la_trigger_position;
  volatile unsigned int la_pre_trigger;
  volatile unsigned int la_post_trigger;
  volatile unsigned int la_filter_control;
  volatile unsigned int la_filter_data;
  volatile unsigned int la_filter_wildcard;
  volatile unsigned int la_triggera_data;
  volatile unsigned int la_triggera_wildcard;
  volatile unsigned int la_triggerb_data;
  volatile unsigned int la_triggerb_wildcard;
  volatile unsigned int la_trigger;
  volatile unsigned int la_fifo;
  volatile unsigned int la[2];
  volatile unsigned int ant_pin;
  volatile unsigned int antd_pin;
  volatile unsigned int gpio_pin;
  volatile unsigned int gpio_h_pin;
  volatile unsigned int bt_pin;
  volatile unsigned int bt_wlan_pin;
  volatile unsigned int si_uart_pin;
  volatile unsigned int clk32k_pin;
  volatile unsigned int reset_tuple_status;
} gpio_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _GPIO_REG_H_ */
