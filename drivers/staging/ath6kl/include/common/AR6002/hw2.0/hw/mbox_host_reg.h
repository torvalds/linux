#ifndef _MBOX_HOST_REG_REG_H_
#define _MBOX_HOST_REG_REG_H_

#define HOST_INT_STATUS_ADDRESS                  0x00000400
#define HOST_INT_STATUS_OFFSET                   0x00000400
#define HOST_INT_STATUS_ERROR_MSB                7
#define HOST_INT_STATUS_ERROR_LSB                7
#define HOST_INT_STATUS_ERROR_MASK               0x00000080
#define HOST_INT_STATUS_ERROR_GET(x)             (((x) & HOST_INT_STATUS_ERROR_MASK) >> HOST_INT_STATUS_ERROR_LSB)
#define HOST_INT_STATUS_ERROR_SET(x)             (((x) << HOST_INT_STATUS_ERROR_LSB) & HOST_INT_STATUS_ERROR_MASK)
#define HOST_INT_STATUS_CPU_MSB                  6
#define HOST_INT_STATUS_CPU_LSB                  6
#define HOST_INT_STATUS_CPU_MASK                 0x00000040
#define HOST_INT_STATUS_CPU_GET(x)               (((x) & HOST_INT_STATUS_CPU_MASK) >> HOST_INT_STATUS_CPU_LSB)
#define HOST_INT_STATUS_CPU_SET(x)               (((x) << HOST_INT_STATUS_CPU_LSB) & HOST_INT_STATUS_CPU_MASK)
#define HOST_INT_STATUS_DRAGON_INT_MSB           5
#define HOST_INT_STATUS_DRAGON_INT_LSB           5
#define HOST_INT_STATUS_DRAGON_INT_MASK          0x00000020
#define HOST_INT_STATUS_DRAGON_INT_GET(x)        (((x) & HOST_INT_STATUS_DRAGON_INT_MASK) >> HOST_INT_STATUS_DRAGON_INT_LSB)
#define HOST_INT_STATUS_DRAGON_INT_SET(x)        (((x) << HOST_INT_STATUS_DRAGON_INT_LSB) & HOST_INT_STATUS_DRAGON_INT_MASK)
#define HOST_INT_STATUS_COUNTER_MSB              4
#define HOST_INT_STATUS_COUNTER_LSB              4
#define HOST_INT_STATUS_COUNTER_MASK             0x00000010
#define HOST_INT_STATUS_COUNTER_GET(x)           (((x) & HOST_INT_STATUS_COUNTER_MASK) >> HOST_INT_STATUS_COUNTER_LSB)
#define HOST_INT_STATUS_COUNTER_SET(x)           (((x) << HOST_INT_STATUS_COUNTER_LSB) & HOST_INT_STATUS_COUNTER_MASK)
#define HOST_INT_STATUS_MBOX_DATA_MSB            3
#define HOST_INT_STATUS_MBOX_DATA_LSB            0
#define HOST_INT_STATUS_MBOX_DATA_MASK           0x0000000f
#define HOST_INT_STATUS_MBOX_DATA_GET(x)         (((x) & HOST_INT_STATUS_MBOX_DATA_MASK) >> HOST_INT_STATUS_MBOX_DATA_LSB)
#define HOST_INT_STATUS_MBOX_DATA_SET(x)         (((x) << HOST_INT_STATUS_MBOX_DATA_LSB) & HOST_INT_STATUS_MBOX_DATA_MASK)

#define CPU_INT_STATUS_ADDRESS                   0x00000401
#define CPU_INT_STATUS_OFFSET                    0x00000401
#define CPU_INT_STATUS_BIT_MSB                   7
#define CPU_INT_STATUS_BIT_LSB                   0
#define CPU_INT_STATUS_BIT_MASK                  0x000000ff
#define CPU_INT_STATUS_BIT_GET(x)                (((x) & CPU_INT_STATUS_BIT_MASK) >> CPU_INT_STATUS_BIT_LSB)
#define CPU_INT_STATUS_BIT_SET(x)                (((x) << CPU_INT_STATUS_BIT_LSB) & CPU_INT_STATUS_BIT_MASK)

#define ERROR_INT_STATUS_ADDRESS                 0x00000402
#define ERROR_INT_STATUS_OFFSET                  0x00000402
#define ERROR_INT_STATUS_SPI_MSB                 3
#define ERROR_INT_STATUS_SPI_LSB                 3
#define ERROR_INT_STATUS_SPI_MASK                0x00000008
#define ERROR_INT_STATUS_SPI_GET(x)              (((x) & ERROR_INT_STATUS_SPI_MASK) >> ERROR_INT_STATUS_SPI_LSB)
#define ERROR_INT_STATUS_SPI_SET(x)              (((x) << ERROR_INT_STATUS_SPI_LSB) & ERROR_INT_STATUS_SPI_MASK)
#define ERROR_INT_STATUS_WAKEUP_MSB              2
#define ERROR_INT_STATUS_WAKEUP_LSB              2
#define ERROR_INT_STATUS_WAKEUP_MASK             0x00000004
#define ERROR_INT_STATUS_WAKEUP_GET(x)           (((x) & ERROR_INT_STATUS_WAKEUP_MASK) >> ERROR_INT_STATUS_WAKEUP_LSB)
#define ERROR_INT_STATUS_WAKEUP_SET(x)           (((x) << ERROR_INT_STATUS_WAKEUP_LSB) & ERROR_INT_STATUS_WAKEUP_MASK)
#define ERROR_INT_STATUS_RX_UNDERFLOW_MSB        1
#define ERROR_INT_STATUS_RX_UNDERFLOW_LSB        1
#define ERROR_INT_STATUS_RX_UNDERFLOW_MASK       0x00000002
#define ERROR_INT_STATUS_RX_UNDERFLOW_GET(x)     (((x) & ERROR_INT_STATUS_RX_UNDERFLOW_MASK) >> ERROR_INT_STATUS_RX_UNDERFLOW_LSB)
#define ERROR_INT_STATUS_RX_UNDERFLOW_SET(x)     (((x) << ERROR_INT_STATUS_RX_UNDERFLOW_LSB) & ERROR_INT_STATUS_RX_UNDERFLOW_MASK)
#define ERROR_INT_STATUS_TX_OVERFLOW_MSB         0
#define ERROR_INT_STATUS_TX_OVERFLOW_LSB         0
#define ERROR_INT_STATUS_TX_OVERFLOW_MASK        0x00000001
#define ERROR_INT_STATUS_TX_OVERFLOW_GET(x)      (((x) & ERROR_INT_STATUS_TX_OVERFLOW_MASK) >> ERROR_INT_STATUS_TX_OVERFLOW_LSB)
#define ERROR_INT_STATUS_TX_OVERFLOW_SET(x)      (((x) << ERROR_INT_STATUS_TX_OVERFLOW_LSB) & ERROR_INT_STATUS_TX_OVERFLOW_MASK)

#define COUNTER_INT_STATUS_ADDRESS               0x00000403
#define COUNTER_INT_STATUS_OFFSET                0x00000403
#define COUNTER_INT_STATUS_COUNTER_MSB           7
#define COUNTER_INT_STATUS_COUNTER_LSB           0
#define COUNTER_INT_STATUS_COUNTER_MASK          0x000000ff
#define COUNTER_INT_STATUS_COUNTER_GET(x)        (((x) & COUNTER_INT_STATUS_COUNTER_MASK) >> COUNTER_INT_STATUS_COUNTER_LSB)
#define COUNTER_INT_STATUS_COUNTER_SET(x)        (((x) << COUNTER_INT_STATUS_COUNTER_LSB) & COUNTER_INT_STATUS_COUNTER_MASK)

#define MBOX_FRAME_ADDRESS                       0x00000404
#define MBOX_FRAME_OFFSET                        0x00000404
#define MBOX_FRAME_RX_EOM_MSB                    7
#define MBOX_FRAME_RX_EOM_LSB                    4
#define MBOX_FRAME_RX_EOM_MASK                   0x000000f0
#define MBOX_FRAME_RX_EOM_GET(x)                 (((x) & MBOX_FRAME_RX_EOM_MASK) >> MBOX_FRAME_RX_EOM_LSB)
#define MBOX_FRAME_RX_EOM_SET(x)                 (((x) << MBOX_FRAME_RX_EOM_LSB) & MBOX_FRAME_RX_EOM_MASK)
#define MBOX_FRAME_RX_SOM_MSB                    3
#define MBOX_FRAME_RX_SOM_LSB                    0
#define MBOX_FRAME_RX_SOM_MASK                   0x0000000f
#define MBOX_FRAME_RX_SOM_GET(x)                 (((x) & MBOX_FRAME_RX_SOM_MASK) >> MBOX_FRAME_RX_SOM_LSB)
#define MBOX_FRAME_RX_SOM_SET(x)                 (((x) << MBOX_FRAME_RX_SOM_LSB) & MBOX_FRAME_RX_SOM_MASK)

#define RX_LOOKAHEAD_VALID_ADDRESS               0x00000405
#define RX_LOOKAHEAD_VALID_OFFSET                0x00000405
#define RX_LOOKAHEAD_VALID_MBOX_MSB              3
#define RX_LOOKAHEAD_VALID_MBOX_LSB              0
#define RX_LOOKAHEAD_VALID_MBOX_MASK             0x0000000f
#define RX_LOOKAHEAD_VALID_MBOX_GET(x)           (((x) & RX_LOOKAHEAD_VALID_MBOX_MASK) >> RX_LOOKAHEAD_VALID_MBOX_LSB)
#define RX_LOOKAHEAD_VALID_MBOX_SET(x)           (((x) << RX_LOOKAHEAD_VALID_MBOX_LSB) & RX_LOOKAHEAD_VALID_MBOX_MASK)

#define RX_LOOKAHEAD0_ADDRESS                    0x00000408
#define RX_LOOKAHEAD0_OFFSET                     0x00000408
#define RX_LOOKAHEAD0_DATA_MSB                   7
#define RX_LOOKAHEAD0_DATA_LSB                   0
#define RX_LOOKAHEAD0_DATA_MASK                  0x000000ff
#define RX_LOOKAHEAD0_DATA_GET(x)                (((x) & RX_LOOKAHEAD0_DATA_MASK) >> RX_LOOKAHEAD0_DATA_LSB)
#define RX_LOOKAHEAD0_DATA_SET(x)                (((x) << RX_LOOKAHEAD0_DATA_LSB) & RX_LOOKAHEAD0_DATA_MASK)

#define RX_LOOKAHEAD1_ADDRESS                    0x0000040c
#define RX_LOOKAHEAD1_OFFSET                     0x0000040c
#define RX_LOOKAHEAD1_DATA_MSB                   7
#define RX_LOOKAHEAD1_DATA_LSB                   0
#define RX_LOOKAHEAD1_DATA_MASK                  0x000000ff
#define RX_LOOKAHEAD1_DATA_GET(x)                (((x) & RX_LOOKAHEAD1_DATA_MASK) >> RX_LOOKAHEAD1_DATA_LSB)
#define RX_LOOKAHEAD1_DATA_SET(x)                (((x) << RX_LOOKAHEAD1_DATA_LSB) & RX_LOOKAHEAD1_DATA_MASK)

#define RX_LOOKAHEAD2_ADDRESS                    0x00000410
#define RX_LOOKAHEAD2_OFFSET                     0x00000410
#define RX_LOOKAHEAD2_DATA_MSB                   7
#define RX_LOOKAHEAD2_DATA_LSB                   0
#define RX_LOOKAHEAD2_DATA_MASK                  0x000000ff
#define RX_LOOKAHEAD2_DATA_GET(x)                (((x) & RX_LOOKAHEAD2_DATA_MASK) >> RX_LOOKAHEAD2_DATA_LSB)
#define RX_LOOKAHEAD2_DATA_SET(x)                (((x) << RX_LOOKAHEAD2_DATA_LSB) & RX_LOOKAHEAD2_DATA_MASK)

#define RX_LOOKAHEAD3_ADDRESS                    0x00000414
#define RX_LOOKAHEAD3_OFFSET                     0x00000414
#define RX_LOOKAHEAD3_DATA_MSB                   7
#define RX_LOOKAHEAD3_DATA_LSB                   0
#define RX_LOOKAHEAD3_DATA_MASK                  0x000000ff
#define RX_LOOKAHEAD3_DATA_GET(x)                (((x) & RX_LOOKAHEAD3_DATA_MASK) >> RX_LOOKAHEAD3_DATA_LSB)
#define RX_LOOKAHEAD3_DATA_SET(x)                (((x) << RX_LOOKAHEAD3_DATA_LSB) & RX_LOOKAHEAD3_DATA_MASK)

#define INT_STATUS_ENABLE_ADDRESS                0x00000418
#define INT_STATUS_ENABLE_OFFSET                 0x00000418
#define INT_STATUS_ENABLE_ERROR_MSB              7
#define INT_STATUS_ENABLE_ERROR_LSB              7
#define INT_STATUS_ENABLE_ERROR_MASK             0x00000080
#define INT_STATUS_ENABLE_ERROR_GET(x)           (((x) & INT_STATUS_ENABLE_ERROR_MASK) >> INT_STATUS_ENABLE_ERROR_LSB)
#define INT_STATUS_ENABLE_ERROR_SET(x)           (((x) << INT_STATUS_ENABLE_ERROR_LSB) & INT_STATUS_ENABLE_ERROR_MASK)
#define INT_STATUS_ENABLE_CPU_MSB                6
#define INT_STATUS_ENABLE_CPU_LSB                6
#define INT_STATUS_ENABLE_CPU_MASK               0x00000040
#define INT_STATUS_ENABLE_CPU_GET(x)             (((x) & INT_STATUS_ENABLE_CPU_MASK) >> INT_STATUS_ENABLE_CPU_LSB)
#define INT_STATUS_ENABLE_CPU_SET(x)             (((x) << INT_STATUS_ENABLE_CPU_LSB) & INT_STATUS_ENABLE_CPU_MASK)
#define INT_STATUS_ENABLE_DRAGON_INT_MSB         5
#define INT_STATUS_ENABLE_DRAGON_INT_LSB         5
#define INT_STATUS_ENABLE_DRAGON_INT_MASK        0x00000020
#define INT_STATUS_ENABLE_DRAGON_INT_GET(x)      (((x) & INT_STATUS_ENABLE_DRAGON_INT_MASK) >> INT_STATUS_ENABLE_DRAGON_INT_LSB)
#define INT_STATUS_ENABLE_DRAGON_INT_SET(x)      (((x) << INT_STATUS_ENABLE_DRAGON_INT_LSB) & INT_STATUS_ENABLE_DRAGON_INT_MASK)
#define INT_STATUS_ENABLE_COUNTER_MSB            4
#define INT_STATUS_ENABLE_COUNTER_LSB            4
#define INT_STATUS_ENABLE_COUNTER_MASK           0x00000010
#define INT_STATUS_ENABLE_COUNTER_GET(x)         (((x) & INT_STATUS_ENABLE_COUNTER_MASK) >> INT_STATUS_ENABLE_COUNTER_LSB)
#define INT_STATUS_ENABLE_COUNTER_SET(x)         (((x) << INT_STATUS_ENABLE_COUNTER_LSB) & INT_STATUS_ENABLE_COUNTER_MASK)
#define INT_STATUS_ENABLE_MBOX_DATA_MSB          3
#define INT_STATUS_ENABLE_MBOX_DATA_LSB          0
#define INT_STATUS_ENABLE_MBOX_DATA_MASK         0x0000000f
#define INT_STATUS_ENABLE_MBOX_DATA_GET(x)       (((x) & INT_STATUS_ENABLE_MBOX_DATA_MASK) >> INT_STATUS_ENABLE_MBOX_DATA_LSB)
#define INT_STATUS_ENABLE_MBOX_DATA_SET(x)       (((x) << INT_STATUS_ENABLE_MBOX_DATA_LSB) & INT_STATUS_ENABLE_MBOX_DATA_MASK)

#define CPU_INT_STATUS_ENABLE_ADDRESS            0x00000419
#define CPU_INT_STATUS_ENABLE_OFFSET             0x00000419
#define CPU_INT_STATUS_ENABLE_BIT_MSB            7
#define CPU_INT_STATUS_ENABLE_BIT_LSB            0
#define CPU_INT_STATUS_ENABLE_BIT_MASK           0x000000ff
#define CPU_INT_STATUS_ENABLE_BIT_GET(x)         (((x) & CPU_INT_STATUS_ENABLE_BIT_MASK) >> CPU_INT_STATUS_ENABLE_BIT_LSB)
#define CPU_INT_STATUS_ENABLE_BIT_SET(x)         (((x) << CPU_INT_STATUS_ENABLE_BIT_LSB) & CPU_INT_STATUS_ENABLE_BIT_MASK)

#define ERROR_STATUS_ENABLE_ADDRESS              0x0000041a
#define ERROR_STATUS_ENABLE_OFFSET               0x0000041a
#define ERROR_STATUS_ENABLE_WAKEUP_MSB           2
#define ERROR_STATUS_ENABLE_WAKEUP_LSB           2
#define ERROR_STATUS_ENABLE_WAKEUP_MASK          0x00000004
#define ERROR_STATUS_ENABLE_WAKEUP_GET(x)        (((x) & ERROR_STATUS_ENABLE_WAKEUP_MASK) >> ERROR_STATUS_ENABLE_WAKEUP_LSB)
#define ERROR_STATUS_ENABLE_WAKEUP_SET(x)        (((x) << ERROR_STATUS_ENABLE_WAKEUP_LSB) & ERROR_STATUS_ENABLE_WAKEUP_MASK)
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_MSB     1
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_LSB     1
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_MASK    0x00000002
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_GET(x)  (((x) & ERROR_STATUS_ENABLE_RX_UNDERFLOW_MASK) >> ERROR_STATUS_ENABLE_RX_UNDERFLOW_LSB)
#define ERROR_STATUS_ENABLE_RX_UNDERFLOW_SET(x)  (((x) << ERROR_STATUS_ENABLE_RX_UNDERFLOW_LSB) & ERROR_STATUS_ENABLE_RX_UNDERFLOW_MASK)
#define ERROR_STATUS_ENABLE_TX_OVERFLOW_MSB      0
#define ERROR_STATUS_ENABLE_TX_OVERFLOW_LSB      0
#define ERROR_STATUS_ENABLE_TX_OVERFLOW_MASK     0x00000001
#define ERROR_STATUS_ENABLE_TX_OVERFLOW_GET(x)   (((x) & ERROR_STATUS_ENABLE_TX_OVERFLOW_MASK) >> ERROR_STATUS_ENABLE_TX_OVERFLOW_LSB)
#define ERROR_STATUS_ENABLE_TX_OVERFLOW_SET(x)   (((x) << ERROR_STATUS_ENABLE_TX_OVERFLOW_LSB) & ERROR_STATUS_ENABLE_TX_OVERFLOW_MASK)

#define COUNTER_INT_STATUS_ENABLE_ADDRESS        0x0000041b
#define COUNTER_INT_STATUS_ENABLE_OFFSET         0x0000041b
#define COUNTER_INT_STATUS_ENABLE_BIT_MSB        7
#define COUNTER_INT_STATUS_ENABLE_BIT_LSB        0
#define COUNTER_INT_STATUS_ENABLE_BIT_MASK       0x000000ff
#define COUNTER_INT_STATUS_ENABLE_BIT_GET(x)     (((x) & COUNTER_INT_STATUS_ENABLE_BIT_MASK) >> COUNTER_INT_STATUS_ENABLE_BIT_LSB)
#define COUNTER_INT_STATUS_ENABLE_BIT_SET(x)     (((x) << COUNTER_INT_STATUS_ENABLE_BIT_LSB) & COUNTER_INT_STATUS_ENABLE_BIT_MASK)

#define COUNT_ADDRESS                            0x00000420
#define COUNT_OFFSET                             0x00000420
#define COUNT_VALUE_MSB                          7
#define COUNT_VALUE_LSB                          0
#define COUNT_VALUE_MASK                         0x000000ff
#define COUNT_VALUE_GET(x)                       (((x) & COUNT_VALUE_MASK) >> COUNT_VALUE_LSB)
#define COUNT_VALUE_SET(x)                       (((x) << COUNT_VALUE_LSB) & COUNT_VALUE_MASK)

#define COUNT_DEC_ADDRESS                        0x00000440
#define COUNT_DEC_OFFSET                         0x00000440
#define COUNT_DEC_VALUE_MSB                      7
#define COUNT_DEC_VALUE_LSB                      0
#define COUNT_DEC_VALUE_MASK                     0x000000ff
#define COUNT_DEC_VALUE_GET(x)                   (((x) & COUNT_DEC_VALUE_MASK) >> COUNT_DEC_VALUE_LSB)
#define COUNT_DEC_VALUE_SET(x)                   (((x) << COUNT_DEC_VALUE_LSB) & COUNT_DEC_VALUE_MASK)

#define SCRATCH_ADDRESS                          0x00000460
#define SCRATCH_OFFSET                           0x00000460
#define SCRATCH_VALUE_MSB                        7
#define SCRATCH_VALUE_LSB                        0
#define SCRATCH_VALUE_MASK                       0x000000ff
#define SCRATCH_VALUE_GET(x)                     (((x) & SCRATCH_VALUE_MASK) >> SCRATCH_VALUE_LSB)
#define SCRATCH_VALUE_SET(x)                     (((x) << SCRATCH_VALUE_LSB) & SCRATCH_VALUE_MASK)

#define FIFO_TIMEOUT_ADDRESS                     0x00000468
#define FIFO_TIMEOUT_OFFSET                      0x00000468
#define FIFO_TIMEOUT_VALUE_MSB                   7
#define FIFO_TIMEOUT_VALUE_LSB                   0
#define FIFO_TIMEOUT_VALUE_MASK                  0x000000ff
#define FIFO_TIMEOUT_VALUE_GET(x)                (((x) & FIFO_TIMEOUT_VALUE_MASK) >> FIFO_TIMEOUT_VALUE_LSB)
#define FIFO_TIMEOUT_VALUE_SET(x)                (((x) << FIFO_TIMEOUT_VALUE_LSB) & FIFO_TIMEOUT_VALUE_MASK)

#define FIFO_TIMEOUT_ENABLE_ADDRESS              0x00000469
#define FIFO_TIMEOUT_ENABLE_OFFSET               0x00000469
#define FIFO_TIMEOUT_ENABLE_SET_MSB              0
#define FIFO_TIMEOUT_ENABLE_SET_LSB              0
#define FIFO_TIMEOUT_ENABLE_SET_MASK             0x00000001
#define FIFO_TIMEOUT_ENABLE_SET_GET(x)           (((x) & FIFO_TIMEOUT_ENABLE_SET_MASK) >> FIFO_TIMEOUT_ENABLE_SET_LSB)
#define FIFO_TIMEOUT_ENABLE_SET_SET(x)           (((x) << FIFO_TIMEOUT_ENABLE_SET_LSB) & FIFO_TIMEOUT_ENABLE_SET_MASK)

#define DISABLE_SLEEP_ADDRESS                    0x0000046a
#define DISABLE_SLEEP_OFFSET                     0x0000046a
#define DISABLE_SLEEP_FOR_INT_MSB                1
#define DISABLE_SLEEP_FOR_INT_LSB                1
#define DISABLE_SLEEP_FOR_INT_MASK               0x00000002
#define DISABLE_SLEEP_FOR_INT_GET(x)             (((x) & DISABLE_SLEEP_FOR_INT_MASK) >> DISABLE_SLEEP_FOR_INT_LSB)
#define DISABLE_SLEEP_FOR_INT_SET(x)             (((x) << DISABLE_SLEEP_FOR_INT_LSB) & DISABLE_SLEEP_FOR_INT_MASK)
#define DISABLE_SLEEP_ON_MSB                     0
#define DISABLE_SLEEP_ON_LSB                     0
#define DISABLE_SLEEP_ON_MASK                    0x00000001
#define DISABLE_SLEEP_ON_GET(x)                  (((x) & DISABLE_SLEEP_ON_MASK) >> DISABLE_SLEEP_ON_LSB)
#define DISABLE_SLEEP_ON_SET(x)                  (((x) << DISABLE_SLEEP_ON_LSB) & DISABLE_SLEEP_ON_MASK)

#define LOCAL_BUS_ADDRESS                        0x00000470
#define LOCAL_BUS_OFFSET                         0x00000470
#define LOCAL_BUS_STATE_MSB                      1
#define LOCAL_BUS_STATE_LSB                      0
#define LOCAL_BUS_STATE_MASK                     0x00000003
#define LOCAL_BUS_STATE_GET(x)                   (((x) & LOCAL_BUS_STATE_MASK) >> LOCAL_BUS_STATE_LSB)
#define LOCAL_BUS_STATE_SET(x)                   (((x) << LOCAL_BUS_STATE_LSB) & LOCAL_BUS_STATE_MASK)

#define INT_WLAN_ADDRESS                         0x00000472
#define INT_WLAN_OFFSET                          0x00000472
#define INT_WLAN_VECTOR_MSB                      7
#define INT_WLAN_VECTOR_LSB                      0
#define INT_WLAN_VECTOR_MASK                     0x000000ff
#define INT_WLAN_VECTOR_GET(x)                   (((x) & INT_WLAN_VECTOR_MASK) >> INT_WLAN_VECTOR_LSB)
#define INT_WLAN_VECTOR_SET(x)                   (((x) << INT_WLAN_VECTOR_LSB) & INT_WLAN_VECTOR_MASK)

#define WINDOW_DATA_ADDRESS                      0x00000474
#define WINDOW_DATA_OFFSET                       0x00000474
#define WINDOW_DATA_DATA_MSB                     7
#define WINDOW_DATA_DATA_LSB                     0
#define WINDOW_DATA_DATA_MASK                    0x000000ff
#define WINDOW_DATA_DATA_GET(x)                  (((x) & WINDOW_DATA_DATA_MASK) >> WINDOW_DATA_DATA_LSB)
#define WINDOW_DATA_DATA_SET(x)                  (((x) << WINDOW_DATA_DATA_LSB) & WINDOW_DATA_DATA_MASK)

#define WINDOW_WRITE_ADDR_ADDRESS                0x00000478
#define WINDOW_WRITE_ADDR_OFFSET                 0x00000478
#define WINDOW_WRITE_ADDR_ADDR_MSB               7
#define WINDOW_WRITE_ADDR_ADDR_LSB               0
#define WINDOW_WRITE_ADDR_ADDR_MASK              0x000000ff
#define WINDOW_WRITE_ADDR_ADDR_GET(x)            (((x) & WINDOW_WRITE_ADDR_ADDR_MASK) >> WINDOW_WRITE_ADDR_ADDR_LSB)
#define WINDOW_WRITE_ADDR_ADDR_SET(x)            (((x) << WINDOW_WRITE_ADDR_ADDR_LSB) & WINDOW_WRITE_ADDR_ADDR_MASK)

#define WINDOW_READ_ADDR_ADDRESS                 0x0000047c
#define WINDOW_READ_ADDR_OFFSET                  0x0000047c
#define WINDOW_READ_ADDR_ADDR_MSB                7
#define WINDOW_READ_ADDR_ADDR_LSB                0
#define WINDOW_READ_ADDR_ADDR_MASK               0x000000ff
#define WINDOW_READ_ADDR_ADDR_GET(x)             (((x) & WINDOW_READ_ADDR_ADDR_MASK) >> WINDOW_READ_ADDR_ADDR_LSB)
#define WINDOW_READ_ADDR_ADDR_SET(x)             (((x) << WINDOW_READ_ADDR_ADDR_LSB) & WINDOW_READ_ADDR_ADDR_MASK)

#define SPI_CONFIG_ADDRESS                       0x00000480
#define SPI_CONFIG_OFFSET                        0x00000480
#define SPI_CONFIG_SPI_RESET_MSB                 4
#define SPI_CONFIG_SPI_RESET_LSB                 4
#define SPI_CONFIG_SPI_RESET_MASK                0x00000010
#define SPI_CONFIG_SPI_RESET_GET(x)              (((x) & SPI_CONFIG_SPI_RESET_MASK) >> SPI_CONFIG_SPI_RESET_LSB)
#define SPI_CONFIG_SPI_RESET_SET(x)              (((x) << SPI_CONFIG_SPI_RESET_LSB) & SPI_CONFIG_SPI_RESET_MASK)
#define SPI_CONFIG_INTERRUPT_ENABLE_MSB          3
#define SPI_CONFIG_INTERRUPT_ENABLE_LSB          3
#define SPI_CONFIG_INTERRUPT_ENABLE_MASK         0x00000008
#define SPI_CONFIG_INTERRUPT_ENABLE_GET(x)       (((x) & SPI_CONFIG_INTERRUPT_ENABLE_MASK) >> SPI_CONFIG_INTERRUPT_ENABLE_LSB)
#define SPI_CONFIG_INTERRUPT_ENABLE_SET(x)       (((x) << SPI_CONFIG_INTERRUPT_ENABLE_LSB) & SPI_CONFIG_INTERRUPT_ENABLE_MASK)
#define SPI_CONFIG_TEST_MODE_MSB                 2
#define SPI_CONFIG_TEST_MODE_LSB                 2
#define SPI_CONFIG_TEST_MODE_MASK                0x00000004
#define SPI_CONFIG_TEST_MODE_GET(x)              (((x) & SPI_CONFIG_TEST_MODE_MASK) >> SPI_CONFIG_TEST_MODE_LSB)
#define SPI_CONFIG_TEST_MODE_SET(x)              (((x) << SPI_CONFIG_TEST_MODE_LSB) & SPI_CONFIG_TEST_MODE_MASK)
#define SPI_CONFIG_DATA_SIZE_MSB                 1
#define SPI_CONFIG_DATA_SIZE_LSB                 0
#define SPI_CONFIG_DATA_SIZE_MASK                0x00000003
#define SPI_CONFIG_DATA_SIZE_GET(x)              (((x) & SPI_CONFIG_DATA_SIZE_MASK) >> SPI_CONFIG_DATA_SIZE_LSB)
#define SPI_CONFIG_DATA_SIZE_SET(x)              (((x) << SPI_CONFIG_DATA_SIZE_LSB) & SPI_CONFIG_DATA_SIZE_MASK)

#define SPI_STATUS_ADDRESS                       0x00000481
#define SPI_STATUS_OFFSET                        0x00000481
#define SPI_STATUS_ADDR_ERR_MSB                  3
#define SPI_STATUS_ADDR_ERR_LSB                  3
#define SPI_STATUS_ADDR_ERR_MASK                 0x00000008
#define SPI_STATUS_ADDR_ERR_GET(x)               (((x) & SPI_STATUS_ADDR_ERR_MASK) >> SPI_STATUS_ADDR_ERR_LSB)
#define SPI_STATUS_ADDR_ERR_SET(x)               (((x) << SPI_STATUS_ADDR_ERR_LSB) & SPI_STATUS_ADDR_ERR_MASK)
#define SPI_STATUS_RD_ERR_MSB                    2
#define SPI_STATUS_RD_ERR_LSB                    2
#define SPI_STATUS_RD_ERR_MASK                   0x00000004
#define SPI_STATUS_RD_ERR_GET(x)                 (((x) & SPI_STATUS_RD_ERR_MASK) >> SPI_STATUS_RD_ERR_LSB)
#define SPI_STATUS_RD_ERR_SET(x)                 (((x) << SPI_STATUS_RD_ERR_LSB) & SPI_STATUS_RD_ERR_MASK)
#define SPI_STATUS_WR_ERR_MSB                    1
#define SPI_STATUS_WR_ERR_LSB                    1
#define SPI_STATUS_WR_ERR_MASK                   0x00000002
#define SPI_STATUS_WR_ERR_GET(x)                 (((x) & SPI_STATUS_WR_ERR_MASK) >> SPI_STATUS_WR_ERR_LSB)
#define SPI_STATUS_WR_ERR_SET(x)                 (((x) << SPI_STATUS_WR_ERR_LSB) & SPI_STATUS_WR_ERR_MASK)
#define SPI_STATUS_READY_MSB                     0
#define SPI_STATUS_READY_LSB                     0
#define SPI_STATUS_READY_MASK                    0x00000001
#define SPI_STATUS_READY_GET(x)                  (((x) & SPI_STATUS_READY_MASK) >> SPI_STATUS_READY_LSB)
#define SPI_STATUS_READY_SET(x)                  (((x) << SPI_STATUS_READY_LSB) & SPI_STATUS_READY_MASK)

#define NON_ASSOC_SLEEP_EN_ADDRESS               0x00000482
#define NON_ASSOC_SLEEP_EN_OFFSET                0x00000482
#define NON_ASSOC_SLEEP_EN_BIT_MSB               0
#define NON_ASSOC_SLEEP_EN_BIT_LSB               0
#define NON_ASSOC_SLEEP_EN_BIT_MASK              0x00000001
#define NON_ASSOC_SLEEP_EN_BIT_GET(x)            (((x) & NON_ASSOC_SLEEP_EN_BIT_MASK) >> NON_ASSOC_SLEEP_EN_BIT_LSB)
#define NON_ASSOC_SLEEP_EN_BIT_SET(x)            (((x) << NON_ASSOC_SLEEP_EN_BIT_LSB) & NON_ASSOC_SLEEP_EN_BIT_MASK)

#define CIS_WINDOW_ADDRESS                       0x00000600
#define CIS_WINDOW_OFFSET                        0x00000600
#define CIS_WINDOW_DATA_MSB                      7
#define CIS_WINDOW_DATA_LSB                      0
#define CIS_WINDOW_DATA_MASK                     0x000000ff
#define CIS_WINDOW_DATA_GET(x)                   (((x) & CIS_WINDOW_DATA_MASK) >> CIS_WINDOW_DATA_LSB)
#define CIS_WINDOW_DATA_SET(x)                   (((x) << CIS_WINDOW_DATA_LSB) & CIS_WINDOW_DATA_MASK)


#ifndef __ASSEMBLER__

typedef struct mbox_host_reg_reg_s {
  unsigned char pad0[1024]; /* pad to 0x400 */
  volatile unsigned char host_int_status;
  volatile unsigned char cpu_int_status;
  volatile unsigned char error_int_status;
  volatile unsigned char counter_int_status;
  volatile unsigned char mbox_frame;
  volatile unsigned char rx_lookahead_valid;
  unsigned char pad1[2]; /* pad to 0x408 */
  volatile unsigned char rx_lookahead0[4];
  volatile unsigned char rx_lookahead1[4];
  volatile unsigned char rx_lookahead2[4];
  volatile unsigned char rx_lookahead3[4];
  volatile unsigned char int_status_enable;
  volatile unsigned char cpu_int_status_enable;
  volatile unsigned char error_status_enable;
  volatile unsigned char counter_int_status_enable;
  unsigned char pad2[4]; /* pad to 0x420 */
  volatile unsigned char count[8];
  unsigned char pad3[24]; /* pad to 0x440 */
  volatile unsigned char count_dec[32];
  volatile unsigned char scratch[8];
  volatile unsigned char fifo_timeout;
  volatile unsigned char fifo_timeout_enable;
  volatile unsigned char disable_sleep;
  unsigned char pad4[5]; /* pad to 0x470 */
  volatile unsigned char local_bus;
  unsigned char pad5[1]; /* pad to 0x472 */
  volatile unsigned char int_wlan;
  unsigned char pad6[1]; /* pad to 0x474 */
  volatile unsigned char window_data[4];
  volatile unsigned char window_write_addr[4];
  volatile unsigned char window_read_addr[4];
  volatile unsigned char spi_config;
  volatile unsigned char spi_status;
  volatile unsigned char non_assoc_sleep_en;
  unsigned char pad7[381]; /* pad to 0x600 */
  volatile unsigned char cis_window[512];
} mbox_host_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _MBOX_HOST_REG_H_ */
