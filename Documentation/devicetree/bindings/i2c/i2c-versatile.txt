i2c Controller on ARM Versatile platform:

Required properties:
- compatible : Must be "arm,versatile-i2c";
- reg
- #address-cells = <1>;
- #size-cells = <0>;

Optional properties:
- Child nodes conforming to i2c bus binding
