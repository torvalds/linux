.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _colorspaces:

***********
Colorspaces
***********

'Color' is a very complex concept and depends on physics, chemistry and
biology. Just because you have three numbers that describe the 'red',
'green' and 'blue' components of the color of a pixel does not mean that
you can accurately display that color. A colorspace defines what it
actually *means* to have an RGB value of e.g. (255, 0, 0). That is,
which color should be reproduced on the screen in a perfectly calibrated
environment.

In order to do that we first need to have a good definition of color,
i.e. some way to uniquely and unambiguously define a color so that
someone else can reproduce it. Human color vision is trichromatic since
the human eye has color receptors that are sensitive to three different
wavelengths of light. Hence the need to use three numbers to describe
color. Be glad you are not a mantis shrimp as those are sensitive to 12
different wavelengths, so instead of RGB we would be using the
ABCDEFGHIJKL colorspace...

Color exists only in the eye and brain and is the result of how strongly
color receptors are stimulated. This is based on the Spectral Power
Distribution (SPD) which is a graph showing the intensity (radiant
power) of the light at wavelengths covering the visible spectrum as it
enters the eye. The science of colorimetry is about the relationship
between the SPD and color as perceived by the human brain.

Since the human eye has only three color receptors it is perfectly
possible that different SPDs will result in the same stimulation of
those receptors and are perceived as the same color, even though the SPD
of the light is different.

In the 1920s experiments were devised to determine the relationship
between SPDs and the perceived color and that resulted in the CIE 1931
standard that defines spectral weighting functions that model the
perception of color. Specifically that standard defines functions that
can take an SPD and calculate the stimulus for each color receptor.
After some further mathematical transforms these stimuli are known as
the *CIE XYZ tristimulus* values and these X, Y and Z values describe a
color as perceived by a human unambiguously. These X, Y and Z values are
all in the range [0…1].

The Y value in the CIE XYZ colorspace corresponds to luminance. Often
the CIE XYZ colorspace is transformed to the normalized CIE xyY
colorspace:

	x = X / (X + Y + Z)

	y = Y / (X + Y + Z)

The x and y values are the chromaticity coordinates and can be used to
define a color without the luminance component Y. It is very confusing
to have such similar names for these colorspaces. Just be aware that if
colors are specified with lower case 'x' and 'y', then the CIE xyY
colorspace is used. Upper case 'X' and 'Y' refer to the CIE XYZ
colorspace. Also, y has nothing to do with luminance. Together x and y
specify a color, and Y the luminance. That is really all you need to
remember from a practical point of view. At the end of this section you
will find reading resources that go into much more detail if you are
interested.

A monitor or TV will reproduce colors by emitting light at three
different wavelengths, the combination of which will stimulate the color
receptors in the eye and thus cause the perception of color.
Historically these wavelengths were defined by the red, green and blue
phosphors used in the displays. These *color primaries* are part of what
defines a colorspace.

Different display devices will have different primaries and some
primaries are more suitable for some display technologies than others.
This has resulted in a variety of colorspaces that are used for
different display technologies or uses. To define a colorspace you need
to define the three color primaries (these are typically defined as x, y
chromaticity coordinates from the CIE xyY colorspace) but also the white
reference: that is the color obtained when all three primaries are at
maximum power. This determines the relative power or energy of the
primaries. This is usually chosen to be close to daylight which has been
defined as the CIE D65 Illuminant.

To recapitulate: the CIE XYZ colorspace uniquely identifies colors.
Other colorspaces are defined by three chromaticity coordinates defined
in the CIE xyY colorspace. Based on those a 3x3 matrix can be
constructed that transforms CIE XYZ colors to colors in the new
colorspace.

Both the CIE XYZ and the RGB colorspace that are derived from the
specific chromaticity primaries are linear colorspaces. But neither the
eye, nor display technology is linear. Doubling the values of all
components in the linear colorspace will not be perceived as twice the
intensity of the color. So each colorspace also defines a transfer
function that takes a linear color component value and transforms it to
the non-linear component value, which is a closer match to the
non-linear performance of both the eye and displays. Linear component
values are denoted RGB, non-linear are denoted as R'G'B'. In general
colors used in graphics are all R'G'B', except in openGL which uses
linear RGB. Special care should be taken when dealing with openGL to
provide linear RGB colors or to use the built-in openGL support to apply
the inverse transfer function.

The final piece that defines a colorspace is a function that transforms
non-linear R'G'B' to non-linear Y'CbCr. This function is determined by
the so-called luma coefficients. There may be multiple possible Y'CbCr
encodings allowed for the same colorspace. Many encodings of color
prefer to use luma (Y') and chroma (CbCr) instead of R'G'B'. Since the
human eye is more sensitive to differences in luminance than in color
this encoding allows one to reduce the amount of color information
compared to the luma data. Note that the luma (Y') is unrelated to the Y
in the CIE XYZ colorspace. Also note that Y'CbCr is often called YCbCr
or YUV even though these are strictly speaking wrong.

Sometimes people confuse Y'CbCr as being a colorspace. This is not
correct, it is just an encoding of an R'G'B' color into luma and chroma
values. The underlying colorspace that is associated with the R'G'B'
color is also associated with the Y'CbCr color.

The final step is how the RGB, R'G'B' or Y'CbCr values are quantized.
The CIE XYZ colorspace where X, Y and Z are in the range [0…1] describes
all colors that humans can perceive, but the transform to another
colorspace will produce colors that are outside the [0…1] range. Once
clamped to the [0…1] range those colors can no longer be reproduced in
that colorspace. This clamping is what reduces the extent or gamut of
the colorspace. How the range of [0…1] is translated to integer values
in the range of [0…255] (or higher, depending on the color depth) is
called the quantization. This is *not* part of the colorspace
definition. In practice RGB or R'G'B' values are full range, i.e. they
use the full [0…255] range. Y'CbCr values on the other hand are limited
range with Y' using [16…235] and Cb and Cr using [16…240].

Unfortunately, in some cases limited range RGB is also used where the
components use the range [16…235]. And full range Y'CbCr also exists
using the [0…255] range.

In order to correctly interpret a color you need to know the
quantization range, whether it is R'G'B' or Y'CbCr, the used Y'CbCr
encoding and the colorspace. From that information you can calculate the
corresponding CIE XYZ color and map that again to whatever colorspace
your display device uses.

The colorspace definition itself consists of the three chromaticity
primaries, the white reference chromaticity, a transfer function and the
luma coefficients needed to transform R'G'B' to Y'CbCr. While some
colorspace standards correctly define all four, quite often the
colorspace standard only defines some, and you have to rely on other
standards for the missing pieces. The fact that colorspaces are often a
mix of different standards also led to very confusing naming conventions
where the name of a standard was used to name a colorspace when in fact
that standard was part of various other colorspaces as well.

If you want to read more about colors and colorspaces, then the
following resources are useful: :ref:`poynton` is a good practical
book for video engineers, :ref:`colimg` has a much broader scope and
describes many more aspects of color (physics, chemistry, biology,
etc.). The
`http://www.brucelindbloom.com <http://www.brucelindbloom.com>`__
website is an excellent resource, especially with respect to the
mathematics behind colorspace conversions. The wikipedia
`CIE 1931 colorspace <http://en.wikipedia.org/wiki/CIE_1931_color_space#CIE_xy_chromaticity_diagram_and_the_CIE_xyY_color_space>`__
article is also very useful.
