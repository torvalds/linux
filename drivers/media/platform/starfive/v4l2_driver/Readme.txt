
/dev/video0: Output the camera data directly.
/dev/video1: Output the data of the camera converted by isp.

ensure linux/arch/riscv/configs/starfive_jh7110_defconfig:
CONFIG_VIDEO_STF_VIN=y
CONFIG_VIN_SENSOR_SC2235=y
CONFIG_VIN_SENSOR_OV4689=y

Only support the lane0/lane5 of dphy as clock lane, lane1/lane2/lane3/lane4
as data lane.
