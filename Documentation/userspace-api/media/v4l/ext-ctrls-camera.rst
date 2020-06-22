.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _camera-controls:

************************
Camera Control Reference
************************

The Camera class includes controls for mechanical (or equivalent
digital) features of a device such as controllable lenses or sensors.


.. _camera-control-id:

Camera Control IDs
==================

``V4L2_CID_CAMERA_CLASS (class)``
    The Camera class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

.. _v4l2-exposure-auto-type:

``V4L2_CID_EXPOSURE_AUTO``
    (enum)

enum v4l2_exposure_auto_type -
    Enables automatic adjustments of the exposure time and/or iris
    aperture. The effect of manual changes of the exposure time or iris
    aperture while these features are enabled is undefined, drivers
    should ignore such requests. Possible values are:



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_EXPOSURE_AUTO``
      - Automatic exposure time, automatic iris aperture.
    * - ``V4L2_EXPOSURE_MANUAL``
      - Manual exposure time, manual iris.
    * - ``V4L2_EXPOSURE_SHUTTER_PRIORITY``
      - Manual exposure time, auto iris.
    * - ``V4L2_EXPOSURE_APERTURE_PRIORITY``
      - Auto exposure time, manual iris.



``V4L2_CID_EXPOSURE_ABSOLUTE (integer)``
    Determines the exposure time of the camera sensor. The exposure time
    is limited by the frame interval. Drivers should interpret the
    values as 100 µs units, where the value 1 stands for 1/10000th of a
    second, 10000 for 1 second and 100000 for 10 seconds.

``V4L2_CID_EXPOSURE_AUTO_PRIORITY (boolean)``
    When ``V4L2_CID_EXPOSURE_AUTO`` is set to ``AUTO`` or
    ``APERTURE_PRIORITY``, this control determines if the device may
    dynamically vary the frame rate. By default this feature is disabled
    (0) and the frame rate must remain constant.

``V4L2_CID_AUTO_EXPOSURE_BIAS (integer menu)``
    Determines the automatic exposure compensation, it is effective only
    when ``V4L2_CID_EXPOSURE_AUTO`` control is set to ``AUTO``,
    ``SHUTTER_PRIORITY`` or ``APERTURE_PRIORITY``. It is expressed in
    terms of EV, drivers should interpret the values as 0.001 EV units,
    where the value 1000 stands for +1 EV.

    Increasing the exposure compensation value is equivalent to
    decreasing the exposure value (EV) and will increase the amount of
    light at the image sensor. The camera performs the exposure
    compensation by adjusting absolute exposure time and/or aperture.

.. _v4l2-exposure-metering:

``V4L2_CID_EXPOSURE_METERING``
    (enum)

enum v4l2_exposure_metering -
    Determines how the camera measures the amount of light available for
    the frame exposure. Possible values are:

.. tabularcolumns:: |p{8.7cm}|p{8.8cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_EXPOSURE_METERING_AVERAGE``
      - Use the light information coming from the entire frame and average
	giving no weighting to any particular portion of the metered area.
    * - ``V4L2_EXPOSURE_METERING_CENTER_WEIGHTED``
      - Average the light information coming from the entire frame giving
	priority to the center of the metered area.
    * - ``V4L2_EXPOSURE_METERING_SPOT``
      - Measure only very small area at the center of the frame.
    * - ``V4L2_EXPOSURE_METERING_MATRIX``
      - A multi-zone metering. The light intensity is measured in several
	points of the frame and the results are combined. The algorithm of
	the zones selection and their significance in calculating the
	final value is device dependent.



``V4L2_CID_PAN_RELATIVE (integer)``
    This control turns the camera horizontally by the specified amount.
    The unit is undefined. A positive value moves the camera to the
    right (clockwise when viewed from above), a negative value to the
    left. A value of zero does not cause motion. This is a write-only
    control.

``V4L2_CID_TILT_RELATIVE (integer)``
    This control turns the camera vertically by the specified amount.
    The unit is undefined. A positive value moves the camera up, a
    negative value down. A value of zero does not cause motion. This is
    a write-only control.

``V4L2_CID_PAN_RESET (button)``
    When this control is set, the camera moves horizontally to the
    default position.

``V4L2_CID_TILT_RESET (button)``
    When this control is set, the camera moves vertically to the default
    position.

``V4L2_CID_PAN_ABSOLUTE (integer)``
    This control turns the camera horizontally to the specified
    position. Positive values move the camera to the right (clockwise
    when viewed from above), negative values to the left. Drivers should
    interpret the values as arc seconds, with valid values between -180
    * 3600 and +180 * 3600 inclusive.

``V4L2_CID_TILT_ABSOLUTE (integer)``
    This control turns the camera vertically to the specified position.
    Positive values move the camera up, negative values down. Drivers
    should interpret the values as arc seconds, with valid values
    between -180 * 3600 and +180 * 3600 inclusive.

``V4L2_CID_FOCUS_ABSOLUTE (integer)``
    This control sets the focal point of the camera to the specified
    position. The unit is undefined. Positive values set the focus
    closer to the camera, negative values towards infinity.

``V4L2_CID_FOCUS_RELATIVE (integer)``
    This control moves the focal point of the camera by the specified
    amount. The unit is undefined. Positive values move the focus closer
    to the camera, negative values towards infinity. This is a
    write-only control.

``V4L2_CID_FOCUS_AUTO (boolean)``
    Enables continuous automatic focus adjustments. The effect of manual
    focus adjustments while this feature is enabled is undefined,
    drivers should ignore such requests.

``V4L2_CID_AUTO_FOCUS_START (button)``
    Starts single auto focus process. The effect of setting this control
    when ``V4L2_CID_FOCUS_AUTO`` is set to ``TRUE`` (1) is undefined,
    drivers should ignore such requests.

``V4L2_CID_AUTO_FOCUS_STOP (button)``
    Aborts automatic focusing started with ``V4L2_CID_AUTO_FOCUS_START``
    control. It is effective only when the continuous autofocus is
    disabled, that is when ``V4L2_CID_FOCUS_AUTO`` control is set to
    ``FALSE`` (0).

.. _v4l2-auto-focus-status:

``V4L2_CID_AUTO_FOCUS_STATUS (bitmask)``
    The automatic focus status. This is a read-only control.

    Setting ``V4L2_LOCK_FOCUS`` lock bit of the ``V4L2_CID_3A_LOCK``
    control may stop updates of the ``V4L2_CID_AUTO_FOCUS_STATUS``
    control value.

.. tabularcolumns:: |p{6.7cm}|p{10.8cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_AUTO_FOCUS_STATUS_IDLE``
      - Automatic focus is not active.
    * - ``V4L2_AUTO_FOCUS_STATUS_BUSY``
      - Automatic focusing is in progress.
    * - ``V4L2_AUTO_FOCUS_STATUS_REACHED``
      - Focus has been reached.
    * - ``V4L2_AUTO_FOCUS_STATUS_FAILED``
      - Automatic focus has failed, the driver will not transition from
	this state until another action is performed by an application.



.. _v4l2-auto-focus-range:

``V4L2_CID_AUTO_FOCUS_RANGE``
    (enum)

enum v4l2_auto_focus_range -
    Determines auto focus distance range for which lens may be adjusted.

.. tabularcolumns:: |p{6.8cm}|p{10.7cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_AUTO_FOCUS_RANGE_AUTO``
      - The camera automatically selects the focus range.
    * - ``V4L2_AUTO_FOCUS_RANGE_NORMAL``
      - Normal distance range, limited for best automatic focus
	performance.
    * - ``V4L2_AUTO_FOCUS_RANGE_MACRO``
      - Macro (close-up) auto focus. The camera will use its minimum
	possible distance for auto focus.
    * - ``V4L2_AUTO_FOCUS_RANGE_INFINITY``
      - The lens is set to focus on an object at infinite distance.



``V4L2_CID_ZOOM_ABSOLUTE (integer)``
    Specify the objective lens focal length as an absolute value. The
    zoom unit is driver-specific and its value should be a positive
    integer.

``V4L2_CID_ZOOM_RELATIVE (integer)``
    Specify the objective lens focal length relatively to the current
    value. Positive values move the zoom lens group towards the
    telephoto direction, negative values towards the wide-angle
    direction. The zoom unit is driver-specific. This is a write-only
    control.

``V4L2_CID_ZOOM_CONTINUOUS (integer)``
    Move the objective lens group at the specified speed until it
    reaches physical device limits or until an explicit request to stop
    the movement. A positive value moves the zoom lens group towards the
    telephoto direction. A value of zero stops the zoom lens group
    movement. A negative value moves the zoom lens group towards the
    wide-angle direction. The zoom speed unit is driver-specific.

``V4L2_CID_IRIS_ABSOLUTE (integer)``
    This control sets the camera's aperture to the specified value. The
    unit is undefined. Larger values open the iris wider, smaller values
    close it.

``V4L2_CID_IRIS_RELATIVE (integer)``
    This control modifies the camera's aperture by the specified amount.
    The unit is undefined. Positive values open the iris one step
    further, negative values close it one step further. This is a
    write-only control.

``V4L2_CID_PRIVACY (boolean)``
    Prevent video from being acquired by the camera. When this control
    is set to ``TRUE`` (1), no image can be captured by the camera.
    Common means to enforce privacy are mechanical obturation of the
    sensor and firmware image processing, but the device is not
    restricted to these methods. Devices that implement the privacy
    control must support read access and may support write access.

``V4L2_CID_BAND_STOP_FILTER (integer)``
    Switch the band-stop filter of a camera sensor on or off, or specify
    its strength. Such band-stop filters can be used, for example, to
    filter out the fluorescent light component.

.. _v4l2-auto-n-preset-white-balance:

``V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE``
    (enum)

enum v4l2_auto_n_preset_white_balance -
    Sets white balance to automatic, manual or a preset. The presets
    determine color temperature of the light as a hint to the camera for
    white balance adjustments resulting in most accurate color
    representation. The following white balance presets are listed in
    order of increasing color temperature.

.. tabularcolumns:: |p{7.2 cm}|p{10.3cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_WHITE_BALANCE_MANUAL``
      - Manual white balance.
    * - ``V4L2_WHITE_BALANCE_AUTO``
      - Automatic white balance adjustments.
    * - ``V4L2_WHITE_BALANCE_INCANDESCENT``
      - White balance setting for incandescent (tungsten) lighting. It
	generally cools down the colors and corresponds approximately to
	2500...3500 K color temperature range.
    * - ``V4L2_WHITE_BALANCE_FLUORESCENT``
      - White balance preset for fluorescent lighting. It corresponds
	approximately to 4000...5000 K color temperature.
    * - ``V4L2_WHITE_BALANCE_FLUORESCENT_H``
      - With this setting the camera will compensate for fluorescent H
	lighting.
    * - ``V4L2_WHITE_BALANCE_HORIZON``
      - White balance setting for horizon daylight. It corresponds
	approximately to 5000 K color temperature.
    * - ``V4L2_WHITE_BALANCE_DAYLIGHT``
      - White balance preset for daylight (with clear sky). It corresponds
	approximately to 5000...6500 K color temperature.
    * - ``V4L2_WHITE_BALANCE_FLASH``
      - With this setting the camera will compensate for the flash light.
	It slightly warms up the colors and corresponds roughly to
	5000...5500 K color temperature.
    * - ``V4L2_WHITE_BALANCE_CLOUDY``
      - White balance preset for moderately overcast sky. This option
	corresponds approximately to 6500...8000 K color temperature
	range.
    * - ``V4L2_WHITE_BALANCE_SHADE``
      - White balance preset for shade or heavily overcast sky. It
	corresponds approximately to 9000...10000 K color temperature.



.. _v4l2-wide-dynamic-range:

``V4L2_CID_WIDE_DYNAMIC_RANGE (boolean)``
    Enables or disables the camera's wide dynamic range feature. This
    feature allows to obtain clear images in situations where intensity
    of the illumination varies significantly throughout the scene, i.e.
    there are simultaneously very dark and very bright areas. It is most
    commonly realized in cameras by combining two subsequent frames with
    different exposure times.  [#f1]_

.. _v4l2-image-stabilization:

``V4L2_CID_IMAGE_STABILIZATION (boolean)``
    Enables or disables image stabilization.

``V4L2_CID_ISO_SENSITIVITY (integer menu)``
    Determines ISO equivalent of an image sensor indicating the sensor's
    sensitivity to light. The numbers are expressed in arithmetic scale,
    as per :ref:`iso12232` standard, where doubling the sensor
    sensitivity is represented by doubling the numerical ISO value.
    Applications should interpret the values as standard ISO values
    multiplied by 1000, e.g. control value 800 stands for ISO 0.8.
    Drivers will usually support only a subset of standard ISO values.
    The effect of setting this control while the
    ``V4L2_CID_ISO_SENSITIVITY_AUTO`` control is set to a value other
    than ``V4L2_CID_ISO_SENSITIVITY_MANUAL`` is undefined, drivers
    should ignore such requests.

.. _v4l2-iso-sensitivity-auto-type:

``V4L2_CID_ISO_SENSITIVITY_AUTO``
    (enum)

enum v4l2_iso_sensitivity_type -
    Enables or disables automatic ISO sensitivity adjustments.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_CID_ISO_SENSITIVITY_MANUAL``
      - Manual ISO sensitivity.
    * - ``V4L2_CID_ISO_SENSITIVITY_AUTO``
      - Automatic ISO sensitivity adjustments.



.. _v4l2-scene-mode:

``V4L2_CID_SCENE_MODE``
    (enum)

enum v4l2_scene_mode -
    This control allows to select scene programs as the camera automatic
    modes optimized for common shooting scenes. Within these modes the
    camera determines best exposure, aperture, focusing, light metering,
    white balance and equivalent sensitivity. The controls of those
    parameters are influenced by the scene mode control. An exact
    behavior in each mode is subject to the camera specification.

    When the scene mode feature is not used, this control should be set
    to ``V4L2_SCENE_MODE_NONE`` to make sure the other possibly related
    controls are accessible. The following scene programs are defined:

.. raw:: latex

    \small

.. tabularcolumns:: |p{5.9cm}|p{11.5cm}|

.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_SCENE_MODE_NONE``
      - The scene mode feature is disabled.
    * - ``V4L2_SCENE_MODE_BACKLIGHT``
      - Backlight. Compensates for dark shadows when light is coming from
	behind a subject, also by automatically turning on the flash.
    * - ``V4L2_SCENE_MODE_BEACH_SNOW``
      - Beach and snow. This mode compensates for all-white or bright
	scenes, which tend to look gray and low contrast, when camera's
	automatic exposure is based on an average scene brightness. To
	compensate, this mode automatically slightly overexposes the
	frames. The white balance may also be adjusted to compensate for
	the fact that reflected snow looks bluish rather than white.
    * - ``V4L2_SCENE_MODE_CANDLELIGHT``
      - Candle light. The camera generally raises the ISO sensitivity and
	lowers the shutter speed. This mode compensates for relatively
	close subject in the scene. The flash is disabled in order to
	preserve the ambiance of the light.
    * - ``V4L2_SCENE_MODE_DAWN_DUSK``
      - Dawn and dusk. Preserves the colors seen in low natural light
	before dusk and after down. The camera may turn off the flash, and
	automatically focus at infinity. It will usually boost saturation
	and lower the shutter speed.
    * - ``V4L2_SCENE_MODE_FALL_COLORS``
      - Fall colors. Increases saturation and adjusts white balance for
	color enhancement. Pictures of autumn leaves get saturated reds
	and yellows.
    * - ``V4L2_SCENE_MODE_FIREWORKS``
      - Fireworks. Long exposure times are used to capture the expanding
	burst of light from a firework. The camera may invoke image
	stabilization.
    * - ``V4L2_SCENE_MODE_LANDSCAPE``
      - Landscape. The camera may choose a small aperture to provide deep
	depth of field and long exposure duration to help capture detail
	in dim light conditions. The focus is fixed at infinity. Suitable
	for distant and wide scenery.
    * - ``V4L2_SCENE_MODE_NIGHT``
      - Night, also known as Night Landscape. Designed for low light
	conditions, it preserves detail in the dark areas without blowing
	out bright objects. The camera generally sets itself to a
	medium-to-high ISO sensitivity, with a relatively long exposure
	time, and turns flash off. As such, there will be increased image
	noise and the possibility of blurred image.
    * - ``V4L2_SCENE_MODE_PARTY_INDOOR``
      - Party and indoor. Designed to capture indoor scenes that are lit
	by indoor background lighting as well as the flash. The camera
	usually increases ISO sensitivity, and adjusts exposure for the
	low light conditions.
    * - ``V4L2_SCENE_MODE_PORTRAIT``
      - Portrait. The camera adjusts the aperture so that the depth of
	field is reduced, which helps to isolate the subject against a
	smooth background. Most cameras recognize the presence of faces in
	the scene and focus on them. The color hue is adjusted to enhance
	skin tones. The intensity of the flash is often reduced.
    * - ``V4L2_SCENE_MODE_SPORTS``
      - Sports. Significantly increases ISO and uses a fast shutter speed
	to freeze motion of rapidly-moving subjects. Increased image noise
	may be seen in this mode.
    * - ``V4L2_SCENE_MODE_SUNSET``
      - Sunset. Preserves deep hues seen in sunsets and sunrises. It bumps
	up the saturation.
    * - ``V4L2_SCENE_MODE_TEXT``
      - Text. It applies extra contrast and sharpness, it is typically a
	black-and-white mode optimized for readability. Automatic focus
	may be switched to close-up mode and this setting may also involve
	some lens-distortion correction.

.. raw:: latex

    \normalsize


``V4L2_CID_3A_LOCK (bitmask)``
    This control locks or unlocks the automatic focus, exposure and
    white balance. The automatic adjustments can be paused independently
    by setting the corresponding lock bit to 1. The camera then retains
    the settings until the lock bit is cleared. The following lock bits
    are defined:

    When a given algorithm is not enabled, drivers should ignore
    requests to lock it and should return no error. An example might be
    an application setting bit ``V4L2_LOCK_WHITE_BALANCE`` when the
    ``V4L2_CID_AUTO_WHITE_BALANCE`` control is set to ``FALSE``. The
    value of this control may be changed by exposure, white balance or
    focus controls.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_LOCK_EXPOSURE``
      - Automatic exposure adjustments lock.
    * - ``V4L2_LOCK_WHITE_BALANCE``
      - Automatic white balance adjustments lock.
    * - ``V4L2_LOCK_FOCUS``
      - Automatic focus lock.



``V4L2_CID_PAN_SPEED (integer)``
    This control turns the camera horizontally at the specific speed.
    The unit is undefined. A positive value moves the camera to the
    right (clockwise when viewed from above), a negative value to the
    left. A value of zero stops the motion if one is in progress and has
    no effect otherwise.

``V4L2_CID_TILT_SPEED (integer)``
    This control turns the camera vertically at the specified speed. The
    unit is undefined. A positive value moves the camera up, a negative
    value down. A value of zero stops the motion if one is in progress
    and has no effect otherwise.

``V4L2_CID_CAMERA_ORIENTATION (menu)``
    This read-only control describes the camera orientation by reporting its
    mounting position on the device where the camera is installed. The control
    value is constant and not modifiable by software. This control is
    particularly meaningful for devices which have a well defined orientation,
    such as phones, laptops and portable devices since the control is expressed
    as a position relative to the device's intended usage orientation. For
    example, a camera installed on the user-facing side of a phone, a tablet or
    a laptop device is said to be have ``V4L2_CAMERA_ORIENTATION_FRONT``
    orientation, while a camera installed on the opposite side of the front one
    is said to be have ``V4L2_CAMERA_ORIENTATION_BACK`` orientation. Camera
    sensors not directly attached to the device, or attached in a way that
    allows them to move freely, such as webcams and digital cameras, are said to
    have the ``V4L2_CAMERA_ORIENTATION_EXTERNAL`` orientation.



.. flat-table::
    :header-rows:  0
    :stub-columns: 0

    * - ``V4L2_CAMERA_ORIENTATION_FRONT``
      - The camera is oriented towards the user facing side of the device.
    * - ``V4L2_CAMERA_ORIENTATION_BACK``
      - The camera is oriented towards the back facing side of the device.
    * - ``V4L2_CAMERA_ORIENTATION_EXTERNAL``
      - The camera is not directly attached to the device and is freely movable.



``V4L2_CID_CAMERA_SENSOR_ROTATION (integer)``
    This read-only control describes the rotation correction in degrees in the
    counter-clockwise direction to be applied to the captured images once
    captured to memory to compensate for the camera sensor mounting rotation.

    For a precise definition of the sensor mounting rotation refer to the
    extensive description of the 'rotation' properties in the device tree
    bindings file 'video-interfaces.txt'.

    A few examples are below reported, using a shark swimming from left to
    right in front of the user as the example scene to capture. ::

                 0               X-axis
               0 +------------------------------------->
                 !
                 !
                 !
                 !           |\____)\___
                 !           ) _____  __`<
                 !           |/     )/
                 !
                 !
                 !
                 V
               Y-axis

    Example one - Webcam

    Assuming you can bring your laptop with you while swimming with sharks,
    the camera module of the laptop is installed on the user facing part of a
    laptop screen casing, and is typically used for video calls. The captured
    images are meant to be displayed in landscape mode (width > height) on the
    laptop screen.

    The camera is typically mounted upside-down to compensate the lens optical
    inversion effect. In this case the value of the
    V4L2_CID_CAMERA_SENSOR_ROTATION control is 0, no rotation is required to
    display images correctly to the user.

    If the camera sensor is not mounted upside-down it is required to compensate
    the lens optical inversion effect and the value of the
    V4L2_CID_CAMERA_SENSOR_ROTATION control is 180 degrees, as images will
    result rotated when captured to memory. ::

                 +--------------------------------------+
                 !                                      !
                 !                                      !
                 !                                      !
                 !              __/(_____/|             !
                 !            >.___  ____ (             !
                 !                 \(    \|             !
                 !                                      !
                 !                                      !
                 !                                      !
                 +--------------------------------------+

    A software rotation correction of 180 degrees has to be applied to correctly
    display the image on the user screen. ::

                 +--------------------------------------+
                 !                                      !
                 !                                      !
                 !                                      !
                 !             |\____)\___              !
                 !             ) _____  __`<            !
                 !             |/     )/                !
                 !                                      !
                 !                                      !
                 !                                      !
                 +--------------------------------------+

    Example two - Phone camera

    It is more handy to go and swim with sharks with only your mobile phone
    with you and take pictures with the camera that is installed on the back
    side of the device, facing away from the user. The captured images are meant
    to be displayed in portrait mode (height > width) to match the device screen
    orientation and the device usage orientation used when taking the picture.

    The camera sensor is typically mounted with its pixel array longer side
    aligned to the device longer side, upside-down mounted to compensate for
    the lens optical inversion effect.

    The images once captured to memory will be rotated and the value of the
    V4L2_CID_CAMERA_SENSOR_ROTATION will report a 90 degree rotation. ::


                 +-------------------------------------+
                 |                 _ _                 |
                 |                \   /                |
                 |                 | |                 |
                 |                 | |                 |
                 |                 |  >                |
                 |                <  |                 |
                 |                 | |                 |
                 |                   .                 |
                 |                  V                  |
                 +-------------------------------------+

    A correction of 90 degrees in counter-clockwise direction has to be
    applied to correctly display the image in portrait mode on the device
    screen. ::

                          +--------------------+
                          |                    |
                          |                    |
                          |                    |
                          |                    |
                          |                    |
                          |                    |
                          |   |\____)\___      |
                          |   ) _____  __`<    |
                          |   |/     )/        |
                          |                    |
                          |                    |
                          |                    |
                          |                    |
                          |                    |
                          +--------------------+


.. [#f1]
   This control may be changed to a menu control in the future, if more
   options are required.
