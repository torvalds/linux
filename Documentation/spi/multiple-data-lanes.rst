====================================
SPI devices with multiple data lanes
====================================

Some specialized SPI controllers and peripherals support multiple data lanes
that allow reading more than one word at a time in parallel. This is different
from dual/quad/octal SPI where multiple bits of a single word are transferred
simultaneously.

For example, controllers that support parallel flash memories have this feature
as do some simultaneous-sampling ADCs where each channel has its own data lane.

---------------------
Describing the wiring
---------------------

The ``spi-tx-bus-width`` and ``spi-rx-bus-width`` properties in the devicetree
are used to describe how many data lanes are connected between the controller
and how wide each lane is. The number of items in the array indicates how many
lanes there are, and the value of each item indicates how many bits wide that
lane is.

For example, a dual-simultaneous-sampling ADC with two 4-bit lanes might be
wired up like this::

    +--------------+    +----------+
    | SPI          |    | AD4630   |
    | Controller   |    | ADC      |
    |              |    |          |
    |          CS0 |--->| CS       |
    |          SCK |--->| SCK      |
    |          SDO |--->| SDI      |
    |              |    |          |
    |        SDIA0 |<---| SDOA0    |
    |        SDIA1 |<---| SDOA1    |
    |        SDIA2 |<---| SDOA2    |
    |        SDIA3 |<---| SDOA3    |
    |              |    |          |
    |        SDIB0 |<---| SDOB0    |
    |        SDIB1 |<---| SDOB1    |
    |        SDIB2 |<---| SDOB2    |
    |        SDIB3 |<---| SDOB3    |
    |              |    |          |
    +--------------+    +----------+

It is described in a devicetree like this::

    spi {
        compatible = "my,spi-controller";

        ...

        adc@0 {
            compatible = "adi,ad4630";
            reg = <0>;
            ...
            spi-rx-bus-width = <4>, <4>; /* 2 lanes of 4 bits each */
            ...
        };
    };

In most cases, lanes will be wired up symmetrically (A to A, B to B, etc). If
this isn't the case, extra ``spi-rx-lane-map`` and ``spi-tx-lane-map``
properties are needed to provide a mapping between controller lanes and the
physical lane wires.

Here is an example where a multi-lane SPI controller has each lane wired to
separate single-lane peripherals::

    +--------------+    +----------+
    | SPI          |    | Thing 1  |
    | Controller   |    |          |
    |              |    |          |
    |          CS0 |--->| CS       |
    |         SDO0 |--->| SDI      |
    |         SDI0 |<---| SDO      |
    |        SCLK0 |--->| SCLK     |
    |              |    |          |
    |              |    +----------+
    |              |
    |              |    +----------+
    |              |    | Thing 2  |
    |              |    |          |
    |          CS1 |--->| CS       |
    |         SDO1 |--->| SDI      |
    |         SDI1 |<---| SDO      |
    |        SCLK1 |--->| SCLK     |
    |              |    |          |
    +--------------+    +----------+

This is described in a devicetree like this::

    spi {
        compatible = "my,spi-controller";

        ...

        thing1@0 {
            compatible = "my,thing1";
            reg = <0>;
            ...
        };

        thing2@1 {
            compatible = "my,thing2";
            reg = <1>;
            ...
            spi-tx-lane-map = <1>; /* lane 0 is not used, lane 1 is used for tx wire */
            spi-rx-lane-map = <1>; /* lane 0 is not used, lane 1 is used for rx wire */
            ...
        };
    };


The default values of ``spi-rx-bus-width`` and ``spi-tx-bus-width`` are ``<1>``,
so these properties can still be omitted even when ``spi-rx-lane-map`` and
``spi-tx-lane-map`` are used.

----------------------------
Usage in a peripheral driver
----------------------------

These types of SPI controllers generally do not support arbitrary use of the
multiple lanes. Instead, they operate in one of a few defined modes. Peripheral
drivers should set the :c:type:`struct spi_transfer.multi_lane_mode <spi_transfer>`
field to indicate which mode they want to use for a given transfer.

The possible values for this field have the following semantics:

- :c:macro:`SPI_MULTI_BUS_MODE_SINGLE`: Only use the first lane. Other lanes are
    ignored. This means that it is operating just like a conventional SPI
    peripheral. This is the default, so it does not need to be explicitly set.

    Example::

        tx_buf[0] = 0x88;

        struct spi_transfer xfer = {
            .tx_buf = tx_buf,
            .len = 1,
        };

        spi_sync_transfer(spi, &xfer, 1);

    Assuming the controller is sending the MSB first, the sequence of bits
    sent over the tx wire would be (right-most bit is sent first)::

        controller    > data bits >     peripheral
        ----------   ----------------   ----------
            SDO 0    0-0-0-1-0-0-0-1    SDI 0

- :c:macro:`SPI_MULTI_BUS_MODE_MIRROR`: Send a single data word over all of the
    lanes at the same time. This only makes sense for writes and not
    for reads.

    Example::

        tx_buf[0] = 0x88;

        struct spi_transfer xfer = {
            .tx_buf = tx_buf,
            .len = 1,
            .multi_lane_mode = SPI_MULTI_BUS_MODE_MIRROR,
        };

        spi_sync_transfer(spi, &xfer, 1);

    The data is mirrored on each tx wire::

        controller    > data bits >     peripheral
        ----------   ----------------   ----------
            SDO 0    0-0-0-1-0-0-0-1    SDI 0
            SDO 1    0-0-0-1-0-0-0-1    SDI 1

- :c:macro:`SPI_MULTI_BUS_MODE_STRIPE`: Send or receive two different data words
    at the same time, one on each lane. This means that the buffer needs to be
    sized to hold data for all lanes. Data is interleaved in the buffer, with
    the first word corresponding to lane 0, the second to lane 1, and so on.
    Once the last lane is used, the next word in the buffer corresponds to lane
    0 again. Accordingly, the buffer size must be a multiple of the number of
    lanes. This mode works for both reads and writes.

    Example::

        struct spi_transfer xfer = {
            .rx_buf = rx_buf,
            .len = 2,
            .multi_lane_mode = SPI_MULTI_BUS_MODE_STRIPE,
        };

        spi_sync_transfer(spi, &xfer, 1);

    Each rx wire has a different data word sent simultaneously::

        controller    < data bits <     peripheral
        ----------   ----------------   ----------
            SDI 0    0-0-0-1-0-0-0-1    SDO 0
            SDI 1    1-0-0-0-1-0-0-0    SDO 1

    After the transfer, ``rx_buf[0] == 0x11`` (word from SDO 0) and
    ``rx_buf[1] == 0x88`` (word from SDO 1).


-----------------------------
SPI controller driver support
-----------------------------

To support multiple data lanes, SPI controller drivers need to set
:c:type:`struct spi_controller.num_data_lanes <spi_controller>` to a value
greater than 1.

Then the part of the driver that handles SPI transfers needs to check the
:c:type:`struct spi_transfer.multi_lane_mode <spi_transfer>` field and implement
the appropriate behavior for each supported mode and return an error for
unsupported modes.

The core SPI code should handle the rest.
