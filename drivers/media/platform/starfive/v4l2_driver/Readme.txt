
/dev/video0: Output the camera data directly.
/dev/video1: Output the data of the camera converted by isp.

ensure linux/arch/riscv/configs/starfive_jh7110_defconfig:
CONFIG_VIDEO_STF_VIN=y
CONFIG_VIN_SENSOR_IMX219=y

Only support the lane4/lane5 of dphy as clock lane, lane0/lane1/lane2/lane3
as data lane.
