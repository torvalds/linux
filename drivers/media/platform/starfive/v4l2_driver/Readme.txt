
/dev/video0 sensor配置为直接输出camera数据的设备节点
/dev/video1 sensor配置为camera数据经ISP处理变成NV12格式的设备节点

确认linux/arch/riscv/configs/starfive_jh7110_defconfig文件
CONFIG_VIDEO_STF_VIN=y
CONFIG_VIN_SENSOR_SC2235=y
CONFIG_VIN_SENSOR_OV4689=y

只支持DPHY的lane0/lane5做clk通道，lane1/2/3/4做数据通道。
