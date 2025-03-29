.. SPDX-License-Identifier: GPL-2.0

=====================================
Devicetree Sources (DTS) Coding Style
=====================================

When writing Devicetree Sources (DTS) please observe below guidelines.  They
should be considered complementary to any rules expressed already in
the Devicetree Specification and the dtc compiler (including W=1 and W=2
builds).

Individual architectures and subarchitectures can define additional rules,
making the coding style stricter.

Naming and Valid Characters
---------------------------

The Devicetree Specification allows a broad range of characters in node
and property names, but this coding style narrows the range down to achieve
better code readability.

1. Node and property names can use only the following characters:

   * Lowercase characters: [a-z]
   * Digits: [0-9]
   * Dash: -

2. Labels can use only the following characters:

   * Lowercase characters: [a-z]
   * Digits: [0-9]
   * Underscore: _

3. Unless a bus defines differently, unit addresses shall use lowercase
   hexadecimal digits, without leading zeros (padding).

4. Hex values in properties, e.g. "reg", shall use lowercase hex.  The address
   part can be padded with leading zeros.

Example::

	gpi_dma2: dma-controller@a00000 {
		compatible = "qcom,sm8550-gpi-dma", "qcom,sm6350-gpi-dma";
		reg = <0x0 0x00a00000 0x0 0x60000>;
	}

Order of Nodes
--------------

1. Nodes on any bus, thus using unit addresses for children, shall be
   ordered by unit address in ascending order.
   Alternatively for some subarchitectures, nodes of the same type can be
   grouped together, e.g. all I2C controllers one after another even if this
   breaks unit address ordering.

2. Nodes without unit addresses shall be ordered alpha-numerically by the node
   name.  For a few node types, they can be ordered by the main property, e.g.
   pin configuration states ordered by value of "pins" property.

3. When extending nodes in the board DTS via &label, the entries shall be
   ordered either alpha-numerically or by keeping the order from DTSI, where
   the choice depends on the subarchitecture.

The above-described ordering rules are easy to enforce during review, reduce
chances of conflicts for simultaneous additions of new nodes to a file and help
in navigating through the DTS source.

Example::

	/* SoC DTSI */

	/ {
		cpus {
			/* ... */
		};

		psci {
			/* ... */
		};

		soc@0 {
			dma: dma-controller@10000 {
				/* ... */
			};

			clk: clock-controller@80000 {
				/* ... */
			};
		};
	};

	/* Board DTS - alphabetical order */

	&clk {
		/* ... */
	};

	&dma {
		/* ... */
	};

	/* Board DTS - alternative order, keep as DTSI */

	&dma {
		/* ... */
	};

	&clk {
		/* ... */
	};

Order of Properties in Device Node
----------------------------------

The following order of properties in device nodes is preferred:

1. "compatible"
2. "reg"
3. "ranges"
4. Standard/common properties (defined by common bindings, e.g. without
   vendor-prefixes)
5. Vendor-specific properties
6. "status" (if applicable)
7. Child nodes, where each node is preceded with a blank line

The "status" property is by default "okay", thus it can be omitted.

The above-described ordering follows this approach:

1. Most important properties start the node: compatible then bus addressing to
   match unit address.
2. Each node will have common properties in similar place.
3. Status is the last information to annotate that device node is or is not
   finished (board resources are needed).

The individual properties inside each group shall use natural sort order by
the property name.

Example::

	/* SoC DTSI */

	device_node: device-class@6789abc {
		compatible = "vendor,device";
		reg = <0x0 0x06789abc 0x0 0xa123>;
		ranges = <0x0 0x0 0x06789abc 0x1000>;
		#dma-cells = <1>;
		clocks = <&clock_controller 0>, <&clock_controller 1>;
		clock-names = "bus", "host";
		#address-cells = <1>;
		#size-cells = <1>;
		vendor,custom-property = <2>;
		status = "disabled";

		child_node: child-class@100 {
			reg = <0x100 0x200>;
			/* ... */
		};
	};

	/* Board DTS */

	&device_node {
		vdd-0v9-supply = <&board_vreg1>;
		vdd-1v8-supply = <&board_vreg4>;
		vdd-3v3-supply = <&board_vreg2>;
		vdd-12v-supply = <&board_vreg3>;
		status = "okay";
	}

Indentation and wrapping
------------------------

1. Use indentation and wrap lines according to
   Documentation/process/coding-style.rst.
2. Each entry in arrays with multiple cells, e.g. "reg" with two IO addresses,
   shall be enclosed in <>.
3. For arrays spanning across lines, it is preferred to split on item boundary
   and align the continued entries with opening < from the first line.
   Usually avoid splitting individual items unless they significantly exceed
   line wrap limit.

Example::

	thermal-sensor@c271000 {
		compatible = "qcom,sm8550-tsens", "qcom,tsens-v2";
		reg = <0x0 0x0c271000 0x0 0x1000>,
		      <0x0 0x0c222000 0x0 0x1000>;
		/* Lines exceeding coding style line wrap limit: */
		interconnects = <&aggre1_noc MASTER_USB3_0 0 &mc_virt SLAVE_EBI1 0>,
				<&gem_noc MASTER_APPSS_PROC 0 &config_noc SLAVE_USB3_0 0>;
	};

Organizing DTSI and DTS
-----------------------

The DTSI and DTS files shall be organized in a way representing the common,
reusable parts of hardware.  Typically, this means organizing DTSI and DTS files
into several files:

1. DTSI with contents of the entire SoC, without nodes for hardware not present
   on the SoC.
2. If applicable: DTSI with common or re-usable parts of the hardware, e.g.
   entire System-on-Module.
3. DTS representing the board.

Hardware components that are present on the board shall be placed in the
board DTS, not in the SoC or SoM DTSI.  A partial exception is a common
external reference SoC input clock, which could be coded as a fixed-clock in
the SoC DTSI with its frequency provided by each board DTS.
