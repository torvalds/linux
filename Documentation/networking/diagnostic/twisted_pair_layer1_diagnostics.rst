.. SPDX-License-Identifier: GPL-2.0

Diagnostic Concept for Investigating Twisted Pair Ethernet Variants at OSI Layer 1
==================================================================================

Introduction
------------

This documentation is designed for two primary audiences:

1. **Users and System Administrators**: For those dealing with real-world
   Ethernet issues, this guide provides a practical, step-by-step
   troubleshooting flow to help identify and resolve common problems in Twisted
   Pair Ethernet at OSI Layer 1. If you're facing unstable links, speed drops,
   or mysterious network issues, jump right into the step-by-step guide and
   follow it through to find your solution.

2. **Kernel Developers**: For developers working with network drivers and PHY
   support, this documentation outlines the diagnostic process and highlights
   areas where the Linux kernel’s diagnostic interfaces could be extended or
   improved. By understanding the diagnostic flow, developers can better
   prioritize future enhancements.

Step-by-Step Diagnostic Guide from Linux (General Ethernet)
-----------------------------------------------------------

This diagnostic guide covers common Ethernet troubleshooting scenarios,
focusing on **link stability and detection** across different Ethernet
environments, including **Single-Pair Ethernet (SPE)** and **Multi-Pair
Ethernet (MPE)**, as well as power delivery technologies like **PoDL** (Power
over Data Line) and **PoE** (Clause 33 PSE).

The guide is designed to help users diagnose physical layer (Layer 1) issues on
systems running **Linux kernel version 6.11 or newer**, utilizing **ethtool
version 6.10 or later** and **iproute2 version 6.4.0 or later**.

In this guide, we assume that users may have **limited or no access to the link
partner** and will focus on diagnosing issues locally.

Diagnostic Scenarios
~~~~~~~~~~~~~~~~~~~~

- **Link is up and stable, but no data transfer**: If the link is stable but
  there are issues with data transmission, refer to the **OSI Layer 2
  Troubleshooting Guide**.

- **Link is unstable**: Link resets, speed drops, or other fluctuations
  indicate potential issues at the hardware or physical layer.

- **No link detected**: The interface is up, but no link is established.

Verify Interface Status
~~~~~~~~~~~~~~~~~~~~~~~

Begin by verifying the status of the Ethernet interface to check if it is
administratively up. Unlike `ethtool`, which provides information on the link
and PHY status, it does not show the **administrative state** of the interface.
To check this, you should use the `ip` command, which describes the interface
state within the angle brackets `"<>"` in its output.

For example, in the output `<NO-CARRIER,BROADCAST,MULTICAST,UP>`, the important
keywords are:

- **UP**: The interface is in the administrative "UP" state.
- **NO-CARRIER**: The interface is administratively up, but no physical link is
  detected.

If the output shows `<BROADCAST,MULTICAST>`, this indicates the interface is in
the administrative "DOWN" state.

- **Command:** `ip link show dev <interface>`

- **Expected Output:**

  .. code-block:: bash

     4: eth0: <NO-CARRIER,BROADCAST,MULTICAST,UP> mtu 1500 ...
        link/ether 88:14:2b:00:96:f2 brd ff:ff:ff:ff:ff:ff

- **Interpreting the Output:**

  - **Administrative UP State**:

    - If the output contains **"UP"**, the interface is administratively up,
      and the system is trying to establish a physical link.

    - If you also see **"NO-CARRIER"**, it means the physical link has not been
      detected, indicating potential Layer 1 issues like a cable fault,
      misconfiguration, or no connection at the link partner. In this case,
      proceed to the **Inspect Link Status and PHY Configuration** section.

  - **Administrative DOWN State**:

    - If the output lacks **"UP"** and shows only states like
      **"<BROADCAST,MULTICAST>"**, it means the interface is administratively
      down. In this case, bring the interface up using the following command:

      .. code-block:: bash

         ip link set dev <interface> up

- **Next Steps**:

  - If the interface is **administratively up** but shows **NO-CARRIER**,
    proceed to the **Inspect Link Status and PHY Configuration** section to
    troubleshoot potential physical layer issues.

  - If the interface was **administratively down** and you have brought it up,
    ensure to **repeat this verification step** to confirm the new state of the
    interface before proceeding

  - **If the interface is up and the link is detected**:

    - If the output shows **"UP"** and there is **no `NO-CARRIER`**, the
      interface is administratively up, and the physical link has been
      successfully established. If everything is working as expected, the Layer
      1 diagnostics are complete, and no further action is needed.

    - If the interface is up and the link is detected but **no data is being
      transferred**, the issue is likely beyond Layer 1, and you should proceed
      with diagnosing the higher layers of the OSI model. This may involve
      checking Layer 2 configurations (such as VLANs or MAC address issues),
      Layer 3 settings (like IP addresses, routing, or ARP), or Layer 4 and
      above (firewalls, services, etc.).

    - If the **link is unstable** or **frequently resetting or dropping**, this
      may indicate a physical layer issue such as a faulty cable, interference,
      or power delivery problems. In this case, proceed with the next step in
      this guide.

Inspect Link Status and PHY Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use `ethtool -I` to check the link status, PHY configuration, supported link
modes, and additional statistics such as the **Link Down Events** counter. This
step is essential for diagnosing Layer 1 problems such as speed mismatches,
duplex issues, and link instability.

For both **Single-Pair Ethernet (SPE)** and **Multi-Pair Ethernet (MPE)**
devices, you will use this step to gather key details about the link. **SPE**
links generally support a single speed and mode without autonegotiation (with
the exception of **10BaseT1L**), while **MPE** devices typically support
multiple link modes and autonegotiation.

- **Command:** `ethtool -I <interface>`

- **Example Output for SPE Interface (Non-autonegotiation)**:

  .. code-block:: bash

     Settings for spe4:
         Supported ports: [ TP ]
         Supported link modes:   100baseT1/Full
         Supported pause frame use: No
         Supports auto-negotiation: No
         Supported FEC modes: Not reported
         Advertised link modes: Not applicable
         Advertised pause frame use: No
         Advertised auto-negotiation: No
         Advertised FEC modes: Not reported
         Speed: 100Mb/s
         Duplex: Full
         Auto-negotiation: off
         master-slave cfg: forced slave
         master-slave status: slave
         Port: Twisted Pair
         PHYAD: 6
         Transceiver: external
         MDI-X: Unknown
         Supports Wake-on: d
         Wake-on: d
         Link detected: yes
         SQI: 7/7
         Link Down Events: 2

- **Example Output for MPE Interface (Autonegotiation)**:

  .. code-block:: bash

     Settings for eth1:
         Supported ports: [ TP    MII ]
         Supported link modes:   10baseT/Half 10baseT/Full
                                 100baseT/Half 100baseT/Full
         Supported pause frame use: Symmetric Receive-only
         Supports auto-negotiation: Yes
         Supported FEC modes: Not reported
         Advertised link modes:  10baseT/Half 10baseT/Full
                                 100baseT/Half 100baseT/Full
         Advertised pause frame use: Symmetric Receive-only
         Advertised auto-negotiation: Yes
         Advertised FEC modes: Not reported
         Link partner advertised link modes:  10baseT/Half 10baseT/Full
                                              100baseT/Half 100baseT/Full
         Link partner advertised pause frame use: Symmetric Receive-only
         Link partner advertised auto-negotiation: Yes
         Link partner advertised FEC modes: Not reported
         Speed: 100Mb/s
         Duplex: Full
         Auto-negotiation: on
         Port: Twisted Pair
         PHYAD: 10
         Transceiver: internal
         MDI-X: Unknown
         Supports Wake-on: pg
         Wake-on: p
         Link detected: yes
         Link Down Events: 1

- **Next Steps**:

  - Record the output provided by `ethtool`, particularly noting the
    **master-slave status**, **speed**, **duplex**, and other relevant fields.
    This information will be useful for further analysis or troubleshooting.
    Once the **ethtool** output has been collected and stored, move on to the
    next diagnostic step.

Check Power Delivery (PoDL or PoE)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If it is known that **PoDL** or **PoE** is **not implemented** on the system,
or the **PSE** (Power Sourcing Equipment) is managed by proprietary user-space
software or external tools, you can skip this step. In such cases, verify power
delivery through alternative methods, such as checking hardware indicators
(LEDs), using multimeters, or consulting vendor-specific software for
monitoring power status.

If **PoDL** or **PoE** is implemented and managed directly by Linux, follow
these steps to ensure power is being delivered correctly:

- **Command:** `ethtool --show-pse <interface>`

- **Expected Output Examples**:

  1. **PSE Not Supported**:

     If no PSE is attached or the interface does not support PSE, the following
     output is expected:

     .. code-block:: bash

        netlink error: No PSE is attached
        netlink error: Operation not supported

  2. **PoDL (Single-Pair Ethernet)**:

     When PoDL is implemented, you might see the following attributes:

     .. code-block:: bash

        PSE attributes for eth1:
        PoDL PSE Admin State: enabled
        PoDL PSE Power Detection Status: delivering power

  3. **PoE (Clause 33 PSE)**:

     For standard PoE, the output may look like this:

     .. code-block:: bash

        PSE attributes for eth1:
        Clause 33 PSE Admin State: enabled
        Clause 33 PSE Power Detection Status: delivering power
        Clause 33 PSE Available Power Limit: 18000

- **Adjust Power Limit (if needed)**:

  - Sometimes, the available power limit may not be sufficient for the link
    partner. You can increase the power limit as needed.

  - **Command:** `ethtool --set-pse <interface> c33-pse-avail-pw-limit <limit>`

    Example:

    .. code-block:: bash

      ethtool --set-pse eth1 c33-pse-avail-pw-limit 18000
      ethtool --show-pse eth1

    **Expected Output** after adjusting the power limit:

    .. code-block:: bash

      Clause 33 PSE Available Power Limit: 18000


- **Next Steps**:

  - **PoE or PoDL Not Used**: If **PoE** or **PoDL** is not implemented or used
    on the system, proceed to the next diagnostic step, as power delivery is
    not relevant for this setup.

  - **PoE or PoDL Controlled Externally**: If **PoE** or **PoDL** is used but
    is not managed by the Linux kernel's **PSE-PD** framework (i.e., it is
    controlled by proprietary user-space software or external tools), this part
    is out of scope for this documentation. Please consult vendor-specific
    documentation or external tools for monitoring and managing power delivery.

  - **PSE Admin State Disabled**:

    - If the `PSE Admin State:` is **disabled**, enable it by running one of
      the following commands:

      .. code-block:: bash

         ethtool --set-pse <devname> podl-pse-admin-control enable

      or, for Clause 33 PSE (PoE):

         ethtool --set-pse <devname> c33-pse-admin-control enable

    - After enabling the PSE Admin State, return to the start of the **Check
      Power Delivery (PoDL or PoE)** step to recheck the power delivery status.

  - **Power Not Delivered**: If the `Power Detection Status` shows something
    other than "delivering power" (e.g., `over current`), troubleshoot the
    **PSE**. Check for potential issues such as a short circuit in the cable,
    insufficient power delivery, or a fault in the PSE itself.

  - **Power Delivered but No Link**: If power is being delivered but no link is
    established, proceed with further diagnostics by performing **Cable
    Diagnostics** or reviewing the **Inspect Link Status and PHY
    Configuration** steps to identify any underlying issues with the physical
    link or settings.

Cable Diagnostics
~~~~~~~~~~~~~~~~~

Use `ethtool` to test for physical layer issues such as cable faults. The test
results can vary depending on the cable's condition, the technology in use, and
the state of the link partner. The results from the cable test will help in
diagnosing issues like open circuits, shorts, impedance mismatches, and
noise-related problems.

- **Command:** `ethtool --cable-test <interface>`

The following are the typical outputs for **Single-Pair Ethernet (SPE)** and
**Multi-Pair Ethernet (MPE)**:

- **For Single-Pair Ethernet (SPE)**:
  - **Expected Output (SPE)**:

  .. code-block:: bash

    Cable test completed for device eth1.
    Pair A, fault length: 25.00m
    Pair A code Open Circuit

  This indicates an open circuit or cable fault at the reported distance, but
  results can be influenced by the link partner's state. Refer to the
  **"Troubleshooting Based on Cable Test Results"** section for further
  interpretation of these results.

- **For Multi-Pair Ethernet (MPE)**:
  - **Expected Output (MPE)**:

  .. code-block:: bash

    Cable test completed for device eth0.
    Pair A code OK
    Pair B code OK
    Pair C code Open Circuit

  Here, Pair C is reported as having an open circuit, while Pairs A and B are
  functioning correctly. However, if autonegotiation is in use on Pairs A and
  B, the cable test may be disrupted. Refer to the **"Troubleshooting Based on
  Cable Test Results"** section for a detailed explanation of these issues and
  how to resolve them.

For detailed descriptions of the different possible cable test results, please
refer to the **"Troubleshooting Based on Cable Test Results"** section.

Troubleshooting Based on Cable Test Results
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

After running the cable test, the results can help identify specific issues in
the physical connection. However, it is important to note that **cable testing
results heavily depend on the capabilities and characteristics of both the
local hardware and the link partner**. The accuracy and reliability of the
results can vary significantly between different hardware implementations.

In some cases, this can introduce **blind spots** in the current cable testing
implementation, where certain results may not accurately reflect the actual
physical state of the cable. For example:

- An **Open Circuit** result might not only indicate a damaged or disconnected
  cable but also occur if the cable is properly attached to a powered-down link
  partner.

- Some PHYs may report a **Short within Pair** if the link partner is in
  **forced slave mode**, even though there is no actual short in the cable.

To help users interpret the results more effectively, it could be beneficial to
extend the **kernel UAPI** (User API) to provide additional context or
**possible variants** of issues based on the hardware’s characteristics. Since
these quirks are often hardware-specific, the **kernel driver** would be an
ideal source of such information. By providing flags or hints related to
potential false positives for each test result, users would have a better
understanding of what to verify and where to investigate further.

Until such improvements are made, users should be aware of these limitations
and manually verify cable issues as needed. Physical inspections may help
resolve uncertainties related to false positive results.

The results can be one of the following:

- **OK**:

  - The cable is functioning correctly, and no issues were detected.

  - **Next Steps**: If you are still experiencing issues, it might be related
    to higher-layer problems, such as duplex mismatches or speed negotiation,
    which are not physical-layer issues.

  - **Special Case for `BaseT1` (1000/100/10BaseT1)**: In `BaseT1` systems, an
    "OK" result typically also means that the link is up and likely in **slave
    mode**, since cable tests usually only pass in this mode. For some
    **10BaseT1L** PHYs, an "OK" result may occur even if the cable is too long
    for the PHY's configured range (for example, when the range is configured
    for short-distance mode).

- **Open Circuit**:

  - An **Open Circuit** result typically indicates that the cable is damaged or
    disconnected at the reported fault length. Consider these possibilities:

    - If the link partner is in **admin down** state or powered off, you might
      still get an "Open Circuit" result even if the cable is functional.

    - **Next Steps**: Inspect the cable at the fault length for visible damage
      or loose connections. Verify the link partner is powered on and in the
      correct mode.

- **Short within Pair**:

  - A **Short within Pair** indicates an unintended connection within the same
    pair of wires, typically caused by physical damage to the cable.

    - **Next Steps**: Replace or repair the cable and check for any physical
      damage or improperly crimped connectors.

- **Short to Another Pair**:

  - A **Short to Another Pair** means the wires from different pairs are
    shorted, which could occur due to physical damage or incorrect wiring.

    - **Next Steps**: Replace or repair the damaged cable. Inspect the cable for
      incorrect terminations or pinched wiring.

- **Impedance Mismatch**:

  - **Impedance Mismatch** indicates a reflection caused by an impedance
    discontinuity in the cable. This can happen when a part of the cable has
    abnormal impedance (e.g., when different cable types are spliced together
    or when there is a defect in the cable).

    - **Next Steps**: Check the cable quality and ensure consistent impedance
      throughout its length. Replace any sections of the cable that do not meet
      specifications.

- **Noise**:

  - **Noise** means that the Time Domain Reflectometry (TDR) test could not
    complete due to excessive noise on the cable, which can be caused by
    interference from electromagnetic sources.

    - **Next Steps**: Identify and eliminate sources of electromagnetic
      interference (EMI) near the cable. Consider using shielded cables or
      rerouting the cable away from noise sources.

- **Resolution Not Possible**:

  - **Resolution Not Possible** means that the TDR test could not detect the
    issue due to the resolution limitations of the test or because the fault is
    beyond the distance that the test can measure.

    - **Next Steps**: Inspect the cable manually if possible, or use alternative
      diagnostic tools that can handle greater distances or higher resolution.

- **Unknown**:

  - An **Unknown** result may occur when the test cannot classify the fault or
    when a specific issue is outside the scope of the tool's detection
    capabilities.

    - **Next Steps**: Re-run the test, verify the link partner's state, and inspect
      the cable manually if necessary.

Verify Link Partner PHY Configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the cable test passes but the link is still not functioning correctly, it’s
essential to verify the configuration of the link partner’s PHY. Mismatches in
speed, duplex settings, or master-slave roles can cause connection issues.

Autonegotiation Mismatch
^^^^^^^^^^^^^^^^^^^^^^^^

- If both link partners support autonegotiation, ensure that autonegotiation is
  enabled on both sides and that all supported link modes are advertised. A
  mismatch can lead to connectivity problems or sub optimal performance.

- **Quick Fix:** Reset autonegotiation to the default settings, which will
  advertise all default link modes:

  .. code-block:: bash

     ethtool -s <interface> autoneg on

- **Command to check configuration:** `ethtool <interface>`

- **Expected Output:** Ensure that both sides advertise compatible link modes.
  If autonegotiation is off, verify that both link partners are configured for
  the same speed and duplex.

  The following example shows a case where the local PHY advertises fewer link
  modes than it supports. This will reduce the number of overlapping link modes
  with the link partner. In the worst case, there will be no common link modes,
  and the link will not be created:

  .. code-block:: bash

     Settings for eth0:
        Supported link modes:  1000baseT/Full, 100baseT/Full
        Advertised link modes: 1000baseT/Full
        Speed: 1000Mb/s
        Duplex: Full
        Auto-negotiation: on

Combined Mode Mismatch (Autonegotiation on One Side, Forced on the Other)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- One possible issue occurs when one side is using **autonegotiation** (as in
  most modern systems), and the other side is set to a **forced link mode**
  (e.g., older hardware with single-speed hubs). In such cases, modern PHYs
  will attempt to detect the forced mode on the other side. If the link is
  established, you may notice:

  - **No or empty "Link partner advertised link modes"**.

  - **"Link partner advertised auto-negotiation:"** will be **"no"** or not
    present.

- This type of detection does not always work reliably:

  - Typically, the modern PHY will default to **Half Duplex**, even if the link
    partner is actually configured for **Full Duplex**.

  - Some PHYs may not work reliably if the link partner switches from one
    forced mode to another. In this case, only a down/up cycle may help.

- **Next Steps**: Set both sides to the same fixed speed and duplex mode to
  avoid potential detection issues.

  .. code-block:: bash

     ethtool -s <interface> speed 1000 duplex full autoneg off

Master/Slave Role Mismatch (BaseT1 and 1000BaseT PHYs)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- In **BaseT1** systems (e.g., 1000BaseT1, 100BaseT1), link establishment
  requires that one device is configured as **master** and the other as
  **slave**. A mismatch in this master-slave configuration can prevent the link
  from being established. However, **1000BaseT** also supports configurable
  master/slave roles and can face similar issues.

- **Role Preference in 1000BaseT**: The **1000BaseT** specification allows link
  partners to negotiate master-slave roles or role preferences during
  autonegotiation. Some PHYs have hardware limitations or bugs that prevent
  them from functioning properly in certain roles. In such cases, drivers may
  force these PHYs into a specific role (e.g., **forced master** or **forced
  slave**) or try a weaker option by setting preferences. If both link partners
  have the same issue and are forced into the same mode (e.g., both forced into
  master mode), they will not be able to establish a link.

- **Next Steps**: Ensure that one side is configured as **master** and the
  other as **slave** to avoid this issue, particularly when hardware
  limitations are involved, or try the weaker **preferred** option instead of
  **forced**. Check for any driver-related restrictions or forced modes.

- **Command to force master/slave mode**:

  .. code-block:: bash

     ethtool -s <interface> master-slave forced-master

  or:

  .. code-block:: bash

     ethtool -s <interface> master-slave forced-master speed 1000 duplex full autoneg off


- **Check the current master/slave status**:

  .. code-block:: bash

     ethtool <interface>

  Example Output:

  .. code-block:: bash

     master-slave cfg: forced-master
     master-slave status: master

- **Hardware Bugs and Driver Forcing**: If a known hardware issue forces the
  PHY into a specific mode, it’s essential to check the driver source code or
  hardware documentation for details. Ensure that the roles are compatible
  across both link partners, and if both PHYs are forced into the same mode,
  adjust one side accordingly to resolve the mismatch.

Monitor Link Resets and Speed Drops
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If the link is unstable, showing frequent resets or speed drops, this may
indicate issues with the cable, PHY configuration, or environmental factors.
While there is still no completely unified way in Linux to directly monitor
downshift events or link speed changes via user space tools, both the Linux
kernel logs and `ethtool` can provide valuable insights, especially if the
driver supports reporting such events.

- **Monitor Kernel Logs for Link Resets and Speed Drops**:

  - The Linux kernel will print link status changes, including downshift
    events, in the system logs. These messages typically include speed changes,
    duplex mode, and downshifted link speed (if the driver supports it).

  - **Command to monitor kernel logs in real-time:**

    .. code-block:: bash

      dmesg -w | grep "Link is Up\|Link is Down"

  - Example Output (if a downshift occurs):

    .. code-block:: bash

      eth0: Link is Up - 100Mbps/Full (downshifted) - flow control rx/tx
      eth0: Link is Down

    This indicates that the link has been established but has downshifted from
    a higher speed.

  - **Note**: Not all drivers or PHYs support downshift reporting, so you may
    not see this information for all devices.

- **Monitor Link Down Events Using `ethtool`**:

  - Starting with the latest kernel and `ethtool` versions, you can track
    **Link Down Events** using the `ethtool -I` command. This will provide
    counters for link drops, helping to diagnose link instability issues if
    supported by the driver.

  - **Command to monitor link down events:**

    .. code-block:: bash

      ethtool -I <interface>

  - Example Output (if supported):

    .. code-block:: bash

      PSE attributes for eth1:
      Link Down Events: 5

    This indicates that the link has dropped 5 times. Frequent link down events
    may indicate cable or environmental issues that require further
    investigation.

- **Check Link Status and Speed**:

  - Even though downshift counts or events are not easily tracked, you can
    still use `ethtool` to manually check the current link speed and status.

  - **Command:** `ethtool <interface>`

  - **Expected Output:**

    .. code-block:: bash

      Speed: 1000Mb/s
      Duplex: Full
      Auto-negotiation: on
      Link detected: yes

    Any inconsistencies in the expected speed or duplex setting could indicate
    an issue.

- **Disable Energy-Efficient Ethernet (EEE) for Diagnostics**:

  - **EEE** (Energy-Efficient Ethernet) can be a source of link instability due
    to transitions in and out of low-power states. For diagnostic purposes, it
    may be useful to **temporarily** disable EEE to determine if it is
    contributing to link instability. This is **not a generic recommendation**
    for disabling power management.

  - **Next Steps**: Disable EEE and monitor if the link becomes stable. If
    disabling EEE resolves the issue, report the bug so that the driver can be
    fixed.

  - **Command:**

    .. code-block:: bash

      ethtool --set-eee <interface> eee off

  - **Important**: If disabling EEE resolves the instability, the issue should
    be reported to the maintainers as a bug, and the driver should be corrected
    to handle EEE properly without causing instability. Disabling EEE
    permanently should not be seen as a solution.

- **Monitor Error Counters**:

  - While some NIC drivers and PHYs provide error counters, there is no unified
    set of PHY-specific counters across all hardware. Additionally, not all
    PHYs provide useful information related to errors like CRC errors, frame
    drops, or link flaps. Therefore, this step is dependent on the specific
    hardware and driver support.

  - **Next Steps**: Use `ethtool -S <interface>` to check if your driver
    provides useful error counters. In some cases, counters may provide
    information about errors like link flaps or physical layer problems (e.g.,
    excessive CRC errors), but results can vary significantly depending on the
    PHY.

  - **Command:** `ethtool -S <interface>`

  - **Example Output (if supported)**:

    .. code-block:: bash

      rx_crc_errors: 123
      tx_errors: 45
      rx_frame_errors: 78

  - **Note**: If no meaningful error counters are available or if counters are
    not supported, you may need to rely on physical inspections (e.g., cable
    condition) or kernel log messages (e.g., link up/down events) to further
    diagnose the issue.

When All Else Fails...
~~~~~~~~~~~~~~~~~~~~~~

So you've checked the cables, monitored the logs, disabled EEE, and still...
nothing? Don’t worry, you’re not alone. Sometimes, Ethernet gremlins just don’t
want to cooperate.

But before you throw in the towel (or the Ethernet cable), take a deep breath.
It’s always possible that:

1. Your PHY has a unique, undocumented personality.

2. The problem is lying dormant, waiting for just the right moment to magically
   resolve itself (hey, it happens!).

3. Or, it could be that the ultimate solution simply hasn’t been invented yet.

If none of the above bring you comfort, there’s one final step: contribute! If
you've uncovered new or unusual issues, or have creative diagnostic methods,
feel free to share your findings and extend this documentation. Together, we
can hunt down every elusive network issue - one twisted pair at a time.

Remember: sometimes the solution is just a reboot away, but if not, it’s time to
dig deeper - or report that bug!

