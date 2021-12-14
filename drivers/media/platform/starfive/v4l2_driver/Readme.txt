

/dev/video0 sensor配置为ov5640的设备节点
/dev/video1 sensor配置为ov4689(i2c0)的设备节点
/dev/video2 sensor配置为sc2235/ov4689(i2c2)的设备节点

确认conf/sdk_210209_defconfig
CONFIG_VIN_SENSOR_OV5640=y
CONFIG_VIN_SENSOR_SC2235=y
CONFIG_VIN_SENSOR_OV4689=y


只支持DPHY的lane0/lane5做clk通道，lane1/2/3/4做数据通道。

sensor port 设为okay, 硬件需要接入对应的sensor，否则驱动不能使用。

1. ov5640 config dts:
	parallel_from_ov5640 port status 设置为okay, sc2235 port status 设为failed.
		port@2 {
			reg = <2>; // dvp sensor

			/* Parallel bus endpoint */
			parallel_from_ov5640: endpoint {
				remote-endpoint = <&ov5640_to_parallel>;
				bus-type = <5>;      /* Parallel */
				bus-width = <8>;
				data-shift = <2>; /* lines 9:2 are used */
				hsync-active = <1>;
				vsync-active = <0>;
				pclk-sample = <1>;
				sensor-type = <0>; //0:SENSOR_VIN 1:SENSOR_ISP0 2:SENSOR_ISP1
				status = "okay";
			};
		};

2. SC2235 config dts:
	stf_isp_hw_ops.c:
		stf_isp_set_format函数里面注释掉：
			// isp_settings = isp_1920_1080_settings;

	parallel_from_sc2235 port status 设置为okay, ov5640/ov4689(i2c2) port status设为failed.
		port@3 {
			reg = <2>; // dvp sensor

			/* Parallel bus endpoint */
			parallel_from_sc2235: endpoint {
				remote-endpoint = <&sc2235_to_parallel>;
				bus-type = <5>;      /* Parallel */
				bus-width = <8>;
				data-shift = <2>; /* lines 9:2 are used */
				hsync-active = <1>;
				vsync-active = <1>;
				pclk-sample = <1>;
				sensor-type = <2>; //0:SENSOR_VIN 1:SENSOR_ISP0 2:SENSOR_ISP1
				status = "okay";
			};
		};

3. i2c0 ov4689 config dts:
	csi2rx0_from_ov4689 port status 设置为okay.
		port@4 {
			reg = <3>; // csi2rx0 sensor

			/* CSI2 bus endpoint */
			csi2rx0_from_ov4689: endpoint {
				remote-endpoint = <&ov4689_to_csi2rx0>;
				bus-type = <4>;      /* MIPI CSI-2 D-PHY */
				clock-lanes = <0>;
				data-lanes = <1 2>;
				sensor-type = <1>; //0:SENSOR_VIN 1:SENSOR_ISP0 2:SENSOR_ISP1
				csi-dt = <0x2b>;
				status = "okay";
			};
		};

4. i2c2 ov4689 config dts:

	stf_isp_hw_ops.c:
		stf_isp_set_format函数里面346行不要注释掉：
			isp_settings = isp_1920_1080_settings;

	csi2rx1_from_ov4689 port status 设置为okay, sc2235 port status 设为failed.
		port@5 {
			reg = <4>; // csi2rx1 sensor

			/* CSI2 bus endpoint */
			csi2rx1_from_ov4689: endpoint {
				remote-endpoint = <&ov4689_to_csi2rx1>;
				bus-type = <4>;      /* MIPI CSI-2 D-PHY */
				clock-lanes = <5>;
				data-lanes = <4 3>;
				lane-polarities = <1 1 1>;
				sensor-type = <2>; //0:SENSOR_VIN 1:SENSOR_ISP0 2:SENSOR_ISP1
				csi-dt = <0x2b>;
				status = "okay";
			};
		};

