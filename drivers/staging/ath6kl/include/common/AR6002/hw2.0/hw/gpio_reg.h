#ifndef _GPIO_REG_REG_H_
#define _GPIO_REG_REG_H_

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

#endif /* _GPIO_REG_H_ */
