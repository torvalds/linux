========================
Multiplane Overlay (MPO)
========================

.. note:: You will get more from this page if you have already read the
   'Documentation/gpu/amdgpu/display/dcn-overview.rst'.


Multiplane Overlay (MPO) allows for multiple framebuffers to be composited via
fixed-function hardware in the display controller rather than using graphics or
compute shaders for composition. This can yield some power savings if it means
the graphics/compute pipelines can be put into low-power states. In summary,
MPO can bring the following benefits:

* Decreased GPU and CPU workload - no composition shaders needed, no extra
  buffer copy needed, GPU can remain idle.
* Plane independent page flips - No need to be tied to global compositor
  page-flip present rate, reduced latency, independent timing.

.. note:: Keep in mind that MPO is all about power-saving; if you want to learn
   more about power-save in the display context, check the link:
   `Power <https://gitlab.freedesktop.org/pq/color-and-hdr/-/blob/main/doc/power.rst>`__.

Multiplane Overlay is only available using the DRM atomic model. The atomic
model only uses a single userspace IOCTL for configuring the display hardware
(modesetting, page-flipping, etc) - drmModeAtomicCommit. To query hardware
resources and limitations userspace also calls into drmModeGetResources which
reports back the number of planes, CRTCs, and connectors. There are three types
of DRM planes that the driver can register and work with:

* ``DRM_PLANE_TYPE_PRIMARY``: Primary planes represent a "main" plane for a
  CRTC, primary planes are the planes operated upon by CRTC modesetting and
  flipping operations.
* ``DRM_PLANE_TYPE_CURSOR``: Cursor planes represent a "cursor" plane for a
  CRTC. Cursor planes are the planes operated upon by the cursor IOCTLs
* ``DRM_PLANE_TYPE_OVERLAY``: Overlay planes represent all non-primary,
  non-cursor planes. Some drivers refer to these types of planes as "sprites"
  internally.

To illustrate how it works, let's take a look at a device that exposes the
following planes to userspace:

* 4 Primary planes (1 per CRTC).
* 4 Cursor planes (1 per CRTC).
* 1 Overlay plane (shared among CRTCs).

.. note:: Keep in mind that different ASICs might expose other numbers of
   planes.

For this hardware example, we have 4 pipes (if you don't know what AMD pipe
means, look at 'Documentation/gpu/amdgpu/display/dcn-overview.rst', section
"AMD Hardware Pipeline"). Typically most AMD devices operate in a pipe-split
configuration for optimal single display output (e.g., 2 pipes per plane).

A typical MPO configuration from userspace - 1 primary + 1 overlay on a single
display - will see 4 pipes in use, 2 per plane.

At least 1 pipe must be used per plane (primary and overlay), so for this
hypothetical hardware that we are using as an example, we have an absolute
limit of 4 planes across all CRTCs. Atomic commits will be rejected for display
configurations using more than 4 planes. Again, it is important to stress that
every DCN has different restrictions; here, we are just trying to provide the
concept idea.

Plane Restrictions
==================

AMDGPU imposes restrictions on the use of DRM planes in the driver.

Atomic commits will be rejected for commits which do not follow these
restrictions:

* Overlay planes must be in ARGB8888 or XRGB8888 format
* Planes cannot be placed outside of the CRTC destination rectangle
* Planes cannot be downscaled more than 1/4x of their original size
* Planes cannot be upscaled more than 16x of their original size

Not every property is available on every plane:

* Only primary planes have color-space and non-RGB format support
* Only overlay planes have alpha blending support

Cursor Restrictions
===================

Before we start to describe some restrictions around cursor and MPO, see the
below image:

.. kernel-figure:: mpo-cursor.svg

The image on the left side represents how DRM expects the cursor and planes to
be blended. However, AMD hardware handles cursors differently, as you can see
on the right side; basically, our cursor cannot be drawn outside its associated
plane as it is being treated as part of the plane. Another consequence of that
is that cursors inherit the color and scale from the plane.

As a result of the above behavior, do not use legacy API to set up the cursor
plane when working with MPO; otherwise, you might encounter unexpected
behavior.

In short, AMD HW has no dedicated cursor planes. A cursor is attached to
another plane and therefore inherits any scaling or color processing from its
parent plane.

Use Cases
=========

Picture-in-Picture (PIP) playback - Underlay strategy
-----------------------------------------------------

Video playback should be done using the "primary plane as underlay" MPO
strategy. This is a 2 planes configuration:

* 1 YUV DRM Primary Plane (e.g. NV12 Video)
* 1 RGBA DRM Overlay Plane (e.g. ARGB8888 desktop). The compositor should
  prepare the framebuffers for the planes as follows:
  - The overlay plane contains general desktop UI, video player controls, and video subtitles
  - Primary plane contains one or more videos

.. note:: Keep in mind that we could extend this configuration to more planes,
   but that is currently not supported by our driver yet (maybe if we have a
   userspace request in the future, we can change that).

See below a single-video example:

.. kernel-figure:: single-display-mpo.svg

.. note:: We could extend this behavior to more planes, but that is currently
   not supported by our driver.

The video buffer should be used directly for the primary plane. The video can
be scaled and positioned for the desktop using the properties: CRTC_X, CRTC_Y,
CRTC_W, and CRTC_H. The primary plane should also have the color encoding and
color range properties set based on the source content:

* ``COLOR_RANGE``, ``COLOR_ENCODING``

The overlay plane should be the native size of the CRTC. The compositor must
draw a transparent cutout for where the video should be placed on the desktop
(i.e., set the alpha to zero). The primary plane video will be visible through
the underlay. The overlay plane's buffer may remain static while the primary
plane's framebuffer is used for standard double-buffered playback.

The compositor should create a YUV buffer matching the native size of the CRTC.
Each video buffer should be composited onto this YUV buffer for direct YUV
scanout. The primary plane should have the color encoding and color range
properties set based on the source content: ``COLOR_RANGE``,
``COLOR_ENCODING``. However, be mindful that the source color space and
encoding match for each video since it affect the entire plane.

The overlay plane should be the native size of the CRTC. The compositor must
draw a transparent cutout for where each video should be placed on the desktop
(i.e., set the alpha to zero). The primary plane videos will be visible through
the underlay. The overlay plane's buffer may remain static while compositing
operations for video playback will be done on the video buffer.

This kernel interface is validated using IGT GPU Tools. The following tests can
be run to validate positioning, blending, scaling under a variety of sequences
and interactions with operations such as DPMS and S3:

- ``kms_plane@plane-panning-bottom-right-pipe-*-planes``
- ``kms_plane@plane-panning-bottom-right-suspend-pipe-*-``
- ``kms_plane@plane-panning-top-left-pipe-*-``
- ``kms_plane@plane-position-covered-pipe-*-``
- ``kms_plane@plane-position-hole-dpms-pipe-*-``
- ``kms_plane@plane-position-hole-pipe-*-``
- ``kms_plane_multiple@atomic-pipe-*-tiling-``
- ``kms_plane_scaling@pipe-*-plane-scaling``
- ``kms_plane_alpha_blend@pipe-*-alpha-basic``
- ``kms_plane_alpha_blend@pipe-*-alpha-transparant-fb``
- ``kms_plane_alpha_blend@pipe-*-alpha-opaque-fb``
- ``kms_plane_alpha_blend@pipe-*-constant-alpha-min``
- ``kms_plane_alpha_blend@pipe-*-constant-alpha-mid``
- ``kms_plane_alpha_blend@pipe-*-constant-alpha-max``

Multiple Display MPO
--------------------

AMDGPU supports display MPO when using multiple displays; however, this feature
behavior heavily relies on the compositor implementation. Keep in mind that
userspace can define different policies. For example, some OSes can use MPO to
protect the plane that handles the video playback; notice that we don't have
many limitations for a single display. Nonetheless, this manipulation can have
many more restrictions for a multi-display scenario. The below example shows a
video playback in the middle of two displays, and it is up to the compositor to
define a policy on how to handle it:

.. kernel-figure:: multi-display-hdcp-mpo.svg

Let's discuss some of the hardware limitations we have when dealing with
multi-display with MPO.

Limitations
~~~~~~~~~~~

For simplicity's sake, for discussing the hardware limitation, this
documentation supposes an example where we have two displays and video playback
that will be moved around different displays.

* **Hardware limitations**

From the DCN overview page, each display requires at least one pipe and each
MPO plane needs another pipe. As a result, when the video is in the middle of
the two displays, we need to use 2 pipes. See the example below where we avoid
pipe split:

- 1 display (1 pipe) + MPO (1 pipe), we will use two pipes
- 2 displays (2 pipes) + MPO (1-2 pipes); we will use 4 pipes. MPO in the
  middle of both displays needs 2 pipes.
- 3 Displays (3 pipes) + MPO (1-2 pipes), we need 5 pipes.

If we use MPO with multiple displays, the userspace has to decide to enable
multiple MPO by the price of limiting the number of external displays supported
or disable it in favor of multiple displays; it is a policy decision. For
example:

* When ASIC has 3 pipes, AMD hardware can NOT support 2 displays with MPO
* When ASIC has 4 pipes, AMD hardware can NOT support 3 displays with MPO

Let's briefly explore how userspace can handle these two display configurations
on an ASIC that only supports three pipes. We can have:

.. kernel-figure:: multi-display-hdcp-mpo-less-pipe-ex.svg

- Total pipes are 3
- User lights up 2 displays (2 out of 3 pipes are used)
- User launches video (1 pipe used for MPO)
- Now, if the user moves the video in the middle of 2 displays, one part of the
  video won't be MPO since we have used 3/3 pipes.

* **Scaling limitation**

MPO cannot handle scaling less than 0.25 and more than x16. For example:

If 4k video (3840x2160) is playing in windowed mode, the physical size of the
window cannot be smaller than (960x540).

.. note:: These scaling limitations might vary from ASIC to ASIC.

* **Size Limitation**

The minimum MPO size is 12px.
