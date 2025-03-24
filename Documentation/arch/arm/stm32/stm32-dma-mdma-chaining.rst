.. SPDX-License-Identifier: GPL-2.0

=======================
STM32 DMA-MDMA chaining
=======================


Introduction
------------

  This document describes the STM32 DMA-MDMA chaining feature. But before going
  further, let's introduce the peripherals involved.

  To offload data transfers from the CPU, STM32 microprocessors (MPUs) embed
  direct memory access controllers (DMA).

  STM32MP1 SoCs embed both STM32 DMA and STM32 MDMA controllers. STM32 DMA
  request routing capabilities are enhanced by a DMA request multiplexer
  (STM32 DMAMUX).

  **STM32 DMAMUX**

  STM32 DMAMUX routes any DMA request from a given peripheral to any STM32 DMA
  controller (STM32MP1 counts two STM32 DMA controllers) channels.

  **STM32 DMA**

  STM32 DMA is mainly used to implement central data buffer storage (usually in
  the system SRAM) for different peripheral. It can access external RAMs but
  without the ability to generate convenient burst transfer ensuring the best
  load of the AXI.

  **STM32 MDMA**

  STM32 MDMA (Master DMA) is mainly used to manage direct data transfers between
  RAM data buffers without CPU intervention. It can also be used in a
  hierarchical structure that uses STM32 DMA as first level data buffer
  interfaces for AHB peripherals, while the STM32 MDMA acts as a second level
  DMA with better performance. As a AXI/AHB master, STM32 MDMA can take control
  of the AXI/AHB bus.


Principles
----------

  STM32 DMA-MDMA chaining feature relies on the strengths of STM32 DMA and
  STM32 MDMA controllers.

  STM32 DMA has a circular Double Buffer Mode (DBM). At each end of transaction
  (when DMA data counter - DMA_SxNDTR - reaches 0), the memory pointers
  (configured with DMA_SxSM0AR and DMA_SxM1AR) are swapped and the DMA data
  counter is automatically reloaded. This allows the SW or the STM32 MDMA to
  process one memory area while the second memory area is being filled/used by
  the STM32 DMA transfer.

  With STM32 MDMA linked-list mode, a single request initiates the data array
  (collection of nodes) to be transferred until the linked-list pointer for the
  channel is null. The channel transfer complete of the last node is the end of
  transfer, unless first and last nodes are linked to each other, in such a
  case, the linked-list loops on to create a circular MDMA transfer.

  STM32 MDMA has direct connections with STM32 DMA. This enables autonomous
  communication and synchronization between peripherals, thus saving CPU
  resources and bus congestion. Transfer Complete signal of STM32 DMA channel
  can triggers STM32 MDMA transfer. STM32 MDMA can clear the request generated
  by the STM32 DMA by writing to its Interrupt Clear register (whose address is
  stored in MDMA_CxMAR, and bit mask in MDMA_CxMDR).

  .. table:: STM32 MDMA interconnect table with STM32 DMA

    +--------------+----------------+-----------+------------+
    | STM32 DMAMUX | STM32 DMA      | STM32 DMA | STM32 MDMA |
    | channels     | channels       | Transfer  | request    |
    |              |                | complete  |            |
    |              |                | signal    |            |
    +==============+================+===========+============+
    | Channel *0*  | DMA1 channel 0 | dma1_tcf0 | *0x00*     |
    +--------------+----------------+-----------+------------+
    | Channel *1*  | DMA1 channel 1 | dma1_tcf1 | *0x01*     |
    +--------------+----------------+-----------+------------+
    | Channel *2*  | DMA1 channel 2 | dma1_tcf2 | *0x02*     |
    +--------------+----------------+-----------+------------+
    | Channel *3*  | DMA1 channel 3 | dma1_tcf3 | *0x03*     |
    +--------------+----------------+-----------+------------+
    | Channel *4*  | DMA1 channel 4 | dma1_tcf4 | *0x04*     |
    +--------------+----------------+-----------+------------+
    | Channel *5*  | DMA1 channel 5 | dma1_tcf5 | *0x05*     |
    +--------------+----------------+-----------+------------+
    | Channel *6*  | DMA1 channel 6 | dma1_tcf6 | *0x06*     |
    +--------------+----------------+-----------+------------+
    | Channel *7*  | DMA1 channel 7 | dma1_tcf7 | *0x07*     |
    +--------------+----------------+-----------+------------+
    | Channel *8*  | DMA2 channel 0 | dma2_tcf0 | *0x08*     |
    +--------------+----------------+-----------+------------+
    | Channel *9*  | DMA2 channel 1 | dma2_tcf1 | *0x09*     |
    +--------------+----------------+-----------+------------+
    | Channel *10* | DMA2 channel 2 | dma2_tcf2 | *0x0A*     |
    +--------------+----------------+-----------+------------+
    | Channel *11* | DMA2 channel 3 | dma2_tcf3 | *0x0B*     |
    +--------------+----------------+-----------+------------+
    | Channel *12* | DMA2 channel 4 | dma2_tcf4 | *0x0C*     |
    +--------------+----------------+-----------+------------+
    | Channel *13* | DMA2 channel 5 | dma2_tcf5 | *0x0D*     |
    +--------------+----------------+-----------+------------+
    | Channel *14* | DMA2 channel 6 | dma2_tcf6 | *0x0E*     |
    +--------------+----------------+-----------+------------+
    | Channel *15* | DMA2 channel 7 | dma2_tcf7 | *0x0F*     |
    +--------------+----------------+-----------+------------+

  STM32 DMA-MDMA chaining feature then uses a SRAM buffer. STM32MP1 SoCs embed
  three fast access static internal RAMs of various size, used for data storage.
  Due to STM32 DMA legacy (within microcontrollers), STM32 DMA performances are
  bad with DDR, while they are optimal with SRAM. Hence the SRAM buffer used
  between STM32 DMA and STM32 MDMA. This buffer is split in two equal periods
  and STM32 DMA uses one period while STM32 MDMA uses the other period
  simultaneously.
  ::

                    dma[1:2]-tcf[0:7]
                   .----------------.
     ____________ '    _________     V____________
    | STM32 DMA  |    /  __|>_  \    | STM32 MDMA |
    |------------|   |  /     \  |   |------------|
    | DMA_SxM0AR |<=>| | SRAM  | |<=>| []-[]...[] |
    | DMA_SxM1AR |   |  \_____/  |   |            |
    |____________|    \___<|____/    |____________|

  STM32 DMA-MDMA chaining uses (struct dma_slave_config).peripheral_config to
  exchange the parameters needed to configure MDMA. These parameters are
  gathered into a u32 array with three values:

  * the STM32 MDMA request (which is actually the DMAMUX channel ID),
  * the address of the STM32 DMA register to clear the Transfer Complete
    interrupt flag,
  * the mask of the Transfer Complete interrupt flag of the STM32 DMA channel.

Device Tree updates for STM32 DMA-MDMA chaining support
-------------------------------------------------------

  **1. Allocate a SRAM buffer**

    SRAM device tree node is defined in SoC device tree. You can refer to it in
    your board device tree to define your SRAM pool.
    ::

          &sram {
                  my_foo_device_dma_pool: dma-sram@0 {
                          reg = <0x0 0x1000>;
                  };
          };

    Be careful of the start index, in case there are other SRAM consumers.
    Define your pool size strategically: to optimise chaining, the idea is that
    STM32 DMA and STM32 MDMA can work simultaneously, on each buffer of the
    SRAM.
    If the SRAM period is greater than the expected DMA transfer, then STM32 DMA
    and STM32 MDMA will work sequentially instead of simultaneously. It is not a
    functional issue but it is not optimal.

    Don't forget to refer to your SRAM pool in your device node. You need to
    define a new property.
    ::

          &my_foo_device {
                  ...
                  my_dma_pool = &my_foo_device_dma_pool;
          };

    Then get this SRAM pool in your foo driver and allocate your SRAM buffer.

  **2. Allocate a STM32 DMA channel and a STM32 MDMA channel**

    You need to define an extra channel in your device tree node, in addition to
    the one you should already have for "classic" DMA operation.

    This new channel must be taken from STM32 MDMA channels, so, the phandle of
    the DMA controller to use is the MDMA controller's one.
    ::

          &my_foo_device {
                  [...]
                  my_dma_pool = &my_foo_device_dma_pool;
                  dmas = <&dmamux1 ...>,                // STM32 DMA channel
                         <&mdma1 0 0x3 0x1200000a 0 0>; // + STM32 MDMA channel
          };

    Concerning STM32 MDMA bindings:

    1. The request line number : whatever the value here, it will be overwritten
    by MDMA driver with the STM32 DMAMUX channel ID passed through
    (struct dma_slave_config).peripheral_config

    2. The priority level : choose Very High (0x3) so that your channel will
    take priority other the other during request arbitration

    3. A 32bit mask specifying the DMA channel configuration : source and
    destination address increment, block transfer with 128 bytes per single
    transfer

    4. The 32bit value specifying the register to be used to acknowledge the
    request: it will be overwritten by MDMA driver, with the DMA channel
    interrupt flag clear register address passed through
    (struct dma_slave_config).peripheral_config

    5. The 32bit mask specifying the value to be written to acknowledge the
    request: it will be overwritten by MDMA driver, with the DMA channel
    Transfer Complete flag passed through
    (struct dma_slave_config).peripheral_config

Driver updates for STM32 DMA-MDMA chaining support in foo driver
----------------------------------------------------------------

  **0. (optional) Refactor the original sg_table if dmaengine_prep_slave_sg()**

    In case of dmaengine_prep_slave_sg(), the original sg_table can't be used as
    is. Two new sg_tables must be created from the original one. One for
    STM32 DMA transfer (where memory address targets now the SRAM buffer instead
    of DDR buffer) and one for STM32 MDMA transfer (where memory address targets
    the DDR buffer).

    The new sg_list items must fit SRAM period length. Here is an example for
    DMA_DEV_TO_MEM:
    ::

      /*
        * Assuming sgl and nents, respectively the initial scatterlist and its
        * length.
        * Assuming sram_dma_buf and sram_period, respectively the memory
        * allocated from the pool for DMA usage, and the length of the period,
        * which is half of the sram_buf size.
        */
      struct sg_table new_dma_sgt, new_mdma_sgt;
      struct scatterlist *s, *_sgl;
      dma_addr_t ddr_dma_buf;
      u32 new_nents = 0, len;
      int i;

      /* Count the number of entries needed */
      for_each_sg(sgl, s, nents, i)
              if (sg_dma_len(s) > sram_period)
                      new_nents += DIV_ROUND_UP(sg_dma_len(s), sram_period);
              else
                      new_nents++;

      /* Create sg table for STM32 DMA channel */
      ret = sg_alloc_table(&new_dma_sgt, new_nents, GFP_ATOMIC);
      if (ret)
              dev_err(dev, "DMA sg table alloc failed\n");

      for_each_sg(new_dma_sgt.sgl, s, new_dma_sgt.nents, i) {
              _sgl = sgl;
              sg_dma_len(s) = min(sg_dma_len(_sgl), sram_period);
              /* Targets the beginning = first half of the sram_buf */
              s->dma_address = sram_buf;
              /*
                * Targets the second half of the sram_buf
                * for odd indexes of the item of the sg_list
                */
              if (i & 1)
                      s->dma_address += sram_period;
      }

      /* Create sg table for STM32 MDMA channel */
      ret = sg_alloc_table(&new_mdma_sgt, new_nents, GFP_ATOMIC);
      if (ret)
              dev_err(dev, "MDMA sg_table alloc failed\n");

      _sgl = sgl;
      len = sg_dma_len(sgl);
      ddr_dma_buf = sg_dma_address(sgl);
      for_each_sg(mdma_sgt.sgl, s, mdma_sgt.nents, i) {
              size_t bytes = min_t(size_t, len, sram_period);

              sg_dma_len(s) = bytes;
              sg_dma_address(s) = ddr_dma_buf;
              len -= bytes;

              if (!len && sg_next(_sgl)) {
                      _sgl = sg_next(_sgl);
                      len = sg_dma_len(_sgl);
                      ddr_dma_buf = sg_dma_address(_sgl);
              } else {
                      ddr_dma_buf += bytes;
              }
      }

    Don't forget to release these new sg_tables after getting the descriptors
    with dmaengine_prep_slave_sg().

  **1. Set controller specific parameters**

    First, use dmaengine_slave_config() with a struct dma_slave_config to
    configure STM32 DMA channel. You just have to take care of DMA addresses,
    the memory address (depending on the transfer direction) must point on your
    SRAM buffer, and set (struct dma_slave_config).peripheral_size != 0.

    STM32 DMA driver will check (struct dma_slave_config).peripheral_size to
    determine if chaining is being used or not. If it is used, then STM32 DMA
    driver fills (struct dma_slave_config).peripheral_config with an array of
    three u32 : the first one containing STM32 DMAMUX channel ID, the second one
    the channel interrupt flag clear register address, and the third one the
    channel Transfer Complete flag mask.

    Then, use dmaengine_slave_config with another struct dma_slave_config to
    configure STM32 MDMA channel. Take care of DMA addresses, the device address
    (depending on the transfer direction) must point on your SRAM buffer, and
    the memory address must point to the buffer originally used for "classic"
    DMA operation. Use the previous (struct dma_slave_config).peripheral_size
    and .peripheral_config that have been updated by STM32 DMA driver, to set
    (struct dma_slave_config).peripheral_size and .peripheral_config of the
    struct dma_slave_config to configure STM32 MDMA channel.
    ::

      struct dma_slave_config dma_conf;
      struct dma_slave_config mdma_conf;

      memset(&dma_conf, 0, sizeof(dma_conf));
      [...]
      config.direction = DMA_DEV_TO_MEM;
      config.dst_addr = sram_dma_buf;        // SRAM buffer
      config.peripheral_size = 1;            // peripheral_size != 0 => chaining

      dmaengine_slave_config(dma_chan, &dma_config);

      memset(&mdma_conf, 0, sizeof(mdma_conf));
      config.direction = DMA_DEV_TO_MEM;
      mdma_conf.src_addr = sram_dma_buf;     // SRAM buffer
      mdma_conf.dst_addr = rx_dma_buf;       // original memory buffer
      mdma_conf.peripheral_size = dma_conf.peripheral_size;       // <- dma_conf
      mdma_conf.peripheral_config = dma_config.peripheral_config; // <- dma_conf

      dmaengine_slave_config(mdma_chan, &mdma_conf);

  **2. Get a descriptor for STM32 DMA channel transaction**

    In the same way you get your descriptor for your "classic" DMA operation,
    you just have to replace the original sg_list (in case of
    dmaengine_prep_slave_sg()) with the new sg_list using SRAM buffer, or to
    replace the original buffer address, length and period (in case of
    dmaengine_prep_dma_cyclic()) with the new SRAM buffer.

  **3. Get a descriptor for STM32 MDMA channel transaction**

    If you previously get descriptor (for STM32 DMA) with

    * dmaengine_prep_slave_sg(), then use dmaengine_prep_slave_sg() for
      STM32 MDMA;
    * dmaengine_prep_dma_cyclic(), then use dmaengine_prep_dma_cyclic() for
      STM32 MDMA.

    Use the new sg_list using SRAM buffer (in case of dmaengine_prep_slave_sg())
    or, depending on the transfer direction, either the original DDR buffer (in
    case of DMA_DEV_TO_MEM) or the SRAM buffer (in case of DMA_MEM_TO_DEV), the
    source address being previously set with dmaengine_slave_config().

  **4. Submit both transactions**

    Before submitting your transactions, you may need to define on which
    descriptor you want a callback to be called at the end of the transfer
    (dmaengine_prep_slave_sg()) or the period (dmaengine_prep_dma_cyclic()).
    Depending on the direction, set the callback on the descriptor that finishes
    the overall transfer:

    * DMA_DEV_TO_MEM: set the callback on the "MDMA" descriptor
    * DMA_MEM_TO_DEV: set the callback on the "DMA" descriptor

    Then, submit the descriptors whatever the order, with dmaengine_tx_submit().

  **5. Issue pending requests (and wait for callback notification)**

  As STM32 MDMA channel transfer is triggered by STM32 DMA, you must issue
  STM32 MDMA channel before STM32 DMA channel.

  If any, your callback will be called to warn you about the end of the overall
  transfer or the period completion.

  Don't forget to terminate both channels. STM32 DMA channel is configured in
  cyclic Double-Buffer mode so it won't be disabled by HW, you need to terminate
  it. STM32 MDMA channel will be stopped by HW in case of sg transfer, but not
  in case of cyclic transfer. You can terminate it whatever the kind of transfer.

  **STM32 DMA-MDMA chaining DMA_MEM_TO_DEV special case**

  STM32 DMA-MDMA chaining in DMA_MEM_TO_DEV is a special case. Indeed, the
  STM32 MDMA feeds the SRAM buffer with the DDR data, and the STM32 DMA reads
  data from SRAM buffer. So some data (the first period) have to be copied in
  SRAM buffer when the STM32 DMA starts to read.

  A trick could be pausing the STM32 DMA channel (that will raise a Transfer
  Complete signal, triggering the STM32 MDMA channel), but the first data read
  by the STM32 DMA could be "wrong". The proper way is to prepare the first SRAM
  period with dmaengine_prep_dma_memcpy(). Then this first period should be
  "removed" from the sg or the cyclic transfer.

  Due to this complexity, rather use the STM32 DMA-MDMA chaining for
  DMA_DEV_TO_MEM and keep the "classic" DMA usage for DMA_MEM_TO_DEV, unless
  you're not afraid.

Resources
---------

  Application note, datasheet and reference manual are available on ST website
  (STM32MP1_).

  Dedicated focus on three application notes (AN5224_, AN4031_ & AN5001_)
  dealing with STM32 DMAMUX, STM32 DMA and STM32 MDMA.

.. _STM32MP1: https://www.st.com/en/microcontrollers-microprocessors/stm32mp1-series.html
.. _AN5224: https://www.st.com/resource/en/application_note/an5224-stm32-dmamux-the-dma-request-router-stmicroelectronics.pdf
.. _AN4031: https://www.st.com/resource/en/application_note/dm00046011-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf
.. _AN5001: https://www.st.com/resource/en/application_note/an5001-stm32cube-expansion-package-for-stm32h7-series-mdma-stmicroelectronics.pdf

:Authors:

- Amelie Delaunay <amelie.delaunay@foss.st.com>