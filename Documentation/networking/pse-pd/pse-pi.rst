.. SPDX-License-Identifier: GPL-2.0

PSE Power Interface (PSE PI) Documentation
==========================================

The Power Sourcing Equipment Power Interface (PSE PI) plays a pivotal role in
the architecture of Power over Ethernet (PoE) systems. It is essentially a
blueprint that outlines how one or multiple power sources are connected to the
eight-pin modular jack, commonly known as the Ethernet RJ45 port. This
connection scheme is crucial for enabling the delivery of power alongside data
over Ethernet cables.

Documentation and Standards
---------------------------

The IEEE 802.3 standard provides detailed documentation on the PSE PI.
Specifically:

- Section "33.2.3 PI pin assignments" covers the pin assignments for PoE
  systems that utilize two pairs for power delivery.
- Section "145.2.4 PSE PI" addresses the configuration for PoE systems that
  deliver power over all four pairs of an Ethernet cable.

PSE PI and Single Pair Ethernet
-------------------------------

Single Pair Ethernet (SPE) represents a different approach to Ethernet
connectivity, utilizing just one pair of conductors for both data and power
transmission. Unlike the configurations detailed in the PSE PI for standard
Ethernet, which can involve multiple power sourcing arrangements across four or
two pairs of wires, SPE operates on a simpler model due to its single-pair
design. As a result, the complexities of choosing between alternative pin
assignments for power delivery, as described in the PSE PI for multi-pair
Ethernet, are not applicable to SPE.

Understanding PSE PI
--------------------

The Power Sourcing Equipment Power Interface (PSE PI) is a framework defining
how Power Sourcing Equipment (PSE) delivers power to Powered Devices (PDs) over
Ethernet cables. It details two main configurations for power delivery, known
as Alternative A and Alternative B, which are distinguished not only by their
method of power transmission but also by the implications for polarity and data
transmission direction.

Alternative A and B Overview
----------------------------

- **Alternative A:** Utilizes RJ45 conductors 1, 2, 3 and 6. In either case of
  networks 10/100BaseT or 1G/2G/5G/10GBaseT, the pairs used are carrying data.
  The power delivery's polarity in this alternative can vary based on the MDI
  (Medium Dependent Interface) or MDI-X (Medium Dependent Interface Crossover)
  configuration.

- **Alternative B:** Utilizes RJ45 conductors 4, 5, 7 and 8. In case of
  10/100BaseT network the pairs used are spare pairs without data and are less
  influenced by data transmission direction. This is not the case for
  1G/2G/5G/10GBaseT network. Alternative B includes two configurations with
  different polarities, known as variant X and variant S, to accommodate
  different network requirements and device specifications.

Table 145-3 PSE Pinout Alternatives
-----------------------------------

The following table outlines the pin configurations for both Alternative A and
Alternative B.

+------------+-------------------+-----------------+-----------------+-----------------+
| Conductor  | Alternative A     | Alternative A   | Alternative B   | Alternative B   |
|            |    (MDI-X)        |      (MDI)      |        (X)      |        (S)      |
+============+===================+=================+=================+=================+
| 1          | Negative V        | Positive V      | -               | -               |
+------------+-------------------+-----------------+-----------------+-----------------+
| 2          | Negative V        | Positive V      | -               | -               |
+------------+-------------------+-----------------+-----------------+-----------------+
| 3          | Positive V        | Negative V      | -               | -               |
+------------+-------------------+-----------------+-----------------+-----------------+
| 4          | -                 | -               | Negative V      | Positive V      |
+------------+-------------------+-----------------+-----------------+-----------------+
| 5          | -                 | -               | Negative V      | Positive V      |
+------------+-------------------+-----------------+-----------------+-----------------+
| 6          | Positive V        | Negative V      | -               | -               |
+------------+-------------------+-----------------+-----------------+-----------------+
| 7          | -                 | -               | Positive V      | Negative V      |
+------------+-------------------+-----------------+-----------------+-----------------+
| 8          | -                 | -               | Positive V      | Negative V      |
+------------+-------------------+-----------------+-----------------+-----------------+

.. note::
    - "Positive V" and "Negative V" indicate the voltage polarity for each pin.
    - "-" indicates that the pin is not used for power delivery in that
      specific configuration.

PSE PI compatibilities
----------------------

The following table outlines the compatibility between the pinout alternative
and the 1000/2.5G/5G/10GBaseT in the PSE 2 pairs connection.

+---------+---------------+---------------------+-----------------------+
| Variant | Alternative   | Power Feeding Type  | Compatibility with    |
|         | (A/B)         | (Direct/Phantom)    | 1000/2.5G/5G/10GBaseT |
+=========+===============+=====================+=======================+
| 1       | A             | Phantom             | Yes                   |
+---------+---------------+---------------------+-----------------------+
| 2       | B             | Phantom             | Yes                   |
+---------+---------------+---------------------+-----------------------+
| 3       | B             | Direct              | No                    |
+---------+---------------+---------------------+-----------------------+

.. note::
    - "Direct" indicate a variant where the power is injected directly to pairs
       without using magnetics in case of spare pairs.
    - "Phantom" indicate power path over coils/magnetics as it is done for
       Alternative A variant.

In case of PSE 4 pairs, a PSE supporting only 10/100BaseT (which mean Direct
Power on pinout Alternative B) is not compatible with a 4 pairs
1000/2.5G/5G/10GBaseT.

PSE Power Interface (PSE PI) Connection Diagram
-----------------------------------------------

The diagram below illustrates the connection architecture between the RJ45
port, the Ethernet PHY (Physical Layer), and the PSE PI (Power Sourcing
Equipment Power Interface), demonstrating how power and data are delivered
simultaneously through an Ethernet cable. The RJ45 port serves as the physical
interface for these connections, with each of its eight pins connected to both
the Ethernet PHY for data transmission and the PSE PI for power delivery.

.. code-block::

    +--------------------------+
    |                          |
    |          RJ45 Port       |
    |                          |
    +--+--+--+--+--+--+--+--+--+                +-------------+
      1| 2| 3| 4| 5| 6| 7| 8|                   |             |
       |  |  |  |  |  |  |  o-------------------+             |
       |  |  |  |  |  |  o--|-------------------+             +<--- PSE 1
       |  |  |  |  |  o--|--|-------------------+             |
       |  |  |  |  o--|--|--|-------------------+             |
       |  |  |  o--|--|--|--|-------------------+  PSE PI     |
       |  |  o--|--|--|--|--|-------------------+             |
       |  o--|--|--|--|--|--|-------------------+             +<--- PSE 2 (optional)
       o--|--|--|--|--|--|--|-------------------+             |
       |  |  |  |  |  |  |  |                   |             |
    +--+--+--+--+--+--+--+--+--+                +-------------+
    |                          |
    |       Ethernet PHY       |
    |                          |
    +--------------------------+

Simple PSE PI Configuration for Alternative A
---------------------------------------------

The diagram below illustrates a straightforward PSE PI (Power Sourcing
Equipment Power Interface) configuration designed to support the Alternative A
setup for Power over Ethernet (PoE). This implementation is tailored to provide
power delivery through the data-carrying pairs of an Ethernet cable, suitable
for either MDI or MDI-X configurations, albeit supporting one variation at a
time.

.. code-block::

         +-------------+
         |    PSE PI   |
 8  -----+                             +-------------+
 7  -----+                    Rail 1   |
 6  -----+------+----------------------+
 5  -----+      |                      |
 4  -----+      |             Rail 2   |  PSE 1
 3  -----+------/         +------------+
 2  -----+--+-------------/            |
 1  -----+--/                          +-------------+
         |
         +-------------+

In this configuration:

- Pins 1 and 2, as well as pins 3 and 6, are utilized for power delivery in
  addition to data transmission. This aligns with the standard wiring for
  10/100BaseT Ethernet networks where these pairs are used for data.
- Rail 1 and Rail 2 represent the positive and negative voltage rails, with
  Rail 1 connected to pins 1 and 2, and Rail 2 connected to pins 3 and 6.
  More advanced PSE PI configurations may include integrated or external
  switches to change the polarity of the voltage rails, allowing for
  compatibility with both MDI and MDI-X configurations.

More complex PSE PI configurations may include additional components, to support
Alternative B, or to provide additional features such as power management, or
additional power delivery capabilities such as 2-pair or 4-pair power delivery.

.. code-block::

         +-------------+
         |    PSE PI   |
         |        +---+
 8  -----+--------+   |                 +-------------+
 7  -----+--------+   |       Rail 1   |
 6  -----+--------+   +-----------------+
 5  -----+--------+   |                |
 4  -----+--------+   |       Rail 2   |  PSE 1
 3  -----+--------+   +----------------+
 2  -----+--------+   |                |
 1  -----+--------+   |                 +-------------+
         |        +---+
         +-------------+

Device Tree Configuration: Describing PSE PI Configurations
-----------------------------------------------------------

The necessity for a separate PSE PI node in the device tree is influenced by
the intricacy of the Power over Ethernet (PoE) system's setup. Here are
descriptions of both simple and complex PSE PI configurations to illustrate
this decision-making process:

**Simple PSE PI Configuration:**
In a straightforward scenario, the PSE PI setup involves a direct, one-to-one
connection between a single PSE controller and an Ethernet port. This setup
typically supports basic PoE functionality without the need for dynamic
configuration or management of multiple power delivery modes. For such simple
configurations, detailing the PSE PI within the existing PSE controller's node
may suffice, as the system does not encompass additional complexity that
warrants a separate node. The primary focus here is on the clear and direct
association of power delivery to a specific Ethernet port.

**Complex PSE PI Configuration:**
Contrastingly, a complex PSE PI setup may encompass multiple PSE controllers or
auxiliary circuits that collectively manage power delivery to one Ethernet
port. Such configurations might support a range of PoE standards and require
the capability to dynamically configure power delivery based on the operational
mode (e.g., PoE2 versus PoE4) or specific requirements of connected devices. In
these instances, a dedicated PSE PI node becomes essential for accurately
documenting the system architecture. This node would serve to detail the
interactions between different PSE controllers, the support for various PoE
modes, and any additional logic required to coordinate power delivery across
the network infrastructure.

**Guidance:**

For simple PSE setups, including PSE PI information in the PSE controller node
might suffice due to the straightforward nature of these systems. However,
complex configurations, involving multiple components or advanced PoE features,
benefit from a dedicated PSE PI node. This method adheres to IEEE 802.3
specifications, improving documentation clarity and ensuring accurate
representation of the PoE system's complexity.

PSE PI Node: Essential Information
----------------------------------

The PSE PI (Power Sourcing Equipment Power Interface) node in a device tree can
include several key pieces of information critical for defining the power
delivery capabilities and configurations of a PoE (Power over Ethernet) system.
Below is a list of such information, along with explanations for their
necessity and reasons why they might not be found within a PSE controller node:

1. **Powered Pairs Configuration**

   - *Description:* Identifies the pairs used for power delivery in the
     Ethernet cable.
   - *Necessity:* Essential to ensure the correct pairs are powered according
     to the board's design.
   - *PSE Controller Node:* Typically lacks details on physical pair usage,
     focusing on power regulation.

2. **Polarity of Powered Pairs**

   - *Description:* Specifies the polarity (positive or negative) for each
     powered pair.
   - *Necessity:* Critical for safe and effective power transmission to PDs.
   - *PSE Controller Node:* Polarity management may exceed the standard
     functionalities of PSE controllers.

3. **PSE Cells Association**

   - *Description:* Details the association of PSE cells with Ethernet ports or
     pairs in multi-cell configurations.
   - *Necessity:* Allows for optimized power resource allocation in complex
     systems.
   - *PSE Controller Node:* Controllers may not manage cell associations
     directly, focusing instead on power flow regulation.

4. **Support for PoE Standards**

   - *Description:* Lists the PoE standards and configurations supported by the
     system.
   - *Necessity:* Ensures system compatibility with various PDs and adherence
     to industry standards.
   - *PSE Controller Node:* Specific capabilities may depend on the overall PSE
     PI design rather than the controller alone. Multiple PSE cells per PI
     do not necessarily imply support for multiple PoE standards.

5. **Protection Mechanisms**

   - *Description:* Outlines additional protection mechanisms, such as
     overcurrent protection and thermal management.
   - *Necessity:* Provides extra safety and stability, complementing PSE
     controller protections.
   - *PSE Controller Node:* Some protections may be implemented via
     board-specific hardware or algorithms external to the controller.
