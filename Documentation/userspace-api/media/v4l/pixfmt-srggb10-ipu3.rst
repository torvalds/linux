.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _v4l2-pix-fmt-ipu3-sbggr10:
.. _v4l2-pix-fmt-ipu3-sgbrg10:
.. _v4l2-pix-fmt-ipu3-sgrbg10:
.. _v4l2-pix-fmt-ipu3-srggb10:

**********************************************************************************************************************************************
V4L2_PIX_FMT_IPU3_SBGGR10 ('ip3b'), V4L2_PIX_FMT_IPU3_SGBRG10 ('ip3g'), V4L2_PIX_FMT_IPU3_SGRBG10 ('ip3G'), V4L2_PIX_FMT_IPU3_SRGGB10 ('ip3r')
**********************************************************************************************************************************************

10-bit Bayer formats

Description
===========

These four pixel formats are used by Intel IPU3 driver, they are raw
sRGB / Bayer formats with 10 bits per sample with every 25 pixels packed
to 32 bytes leaving 6 most significant bits padding in the last byte.
The format is little endian.

In other respects this format is similar to :ref:`V4L2-PIX-FMT-SRGGB10`.
Below is an example of a small image in V4L2_PIX_FMT_IPU3_SBGGR10 format.

**Byte Order.**
Each cell is one byte.

.. tabularcolumns:: |p{0.8cm}|p{4.0cm}|p{4.0cm}|p{4.0cm}|p{4.0cm}|

.. flat-table::

    * - start + 0:
      - B\ :sub:`0000low`
      - G\ :sub:`0001low`\ (bits 7--2)

        B\ :sub:`0000high`\ (bits 1--0)
      - B\ :sub:`0002low`\ (bits 7--4)

        G\ :sub:`0001high`\ (bits 3--0)
      - G\ :sub:`0003low`\ (bits 7--6)

        B\ :sub:`0002high`\ (bits 5--0)
    * - start + 4:
      - G\ :sub:`0003high`
      - B\ :sub:`0004low`
      - G\ :sub:`0005low`\ (bits 7--2)

        B\ :sub:`0004high`\ (bits 1--0)
      - B\ :sub:`0006low`\ (bits 7--4)

        G\ :sub:`0005high`\ (bits 3--0)
    * - start + 8:
      - G\ :sub:`0007low`\ (bits 7--6)

        B\ :sub:`0006high`\ (bits 5--0)
      - G\ :sub:`0007high`
      - B\ :sub:`0008low`
      - G\ :sub:`0009low`\ (bits 7--2)

        B\ :sub:`0008high`\ (bits 1--0)
    * - start + 12:
      - B\ :sub:`0010low`\ (bits 7--4)

        G\ :sub:`0009high`\ (bits 3--0)
      - G\ :sub:`0011low`\ (bits 7--6)

        B\ :sub:`0010high`\ (bits 5--0)
      - G\ :sub:`0011high`
      - B\ :sub:`0012low`
    * - start + 16:
      - G\ :sub:`0013low`\ (bits 7--2)

        B\ :sub:`0012high`\ (bits 1--0)
      - B\ :sub:`0014low`\ (bits 7--4)

        G\ :sub:`0013high`\ (bits 3--0)
      - G\ :sub:`0015low`\ (bits 7--6)

        B\ :sub:`0014high`\ (bits 5--0)
      - G\ :sub:`0015high`
    * - start + 20
      - B\ :sub:`0016low`
      - G\ :sub:`0017low`\ (bits 7--2)

        B\ :sub:`0016high`\ (bits 1--0)
      - B\ :sub:`0018low`\ (bits 7--4)

        G\ :sub:`0017high`\ (bits 3--0)
      - G\ :sub:`0019low`\ (bits 7--6)

        B\ :sub:`0018high`\ (bits 5--0)
    * - start + 24:
      - G\ :sub:`0019high`
      - B\ :sub:`0020low`
      - G\ :sub:`0021low`\ (bits 7--2)

        B\ :sub:`0020high`\ (bits 1--0)
      - B\ :sub:`0022low`\ (bits 7--4)

        G\ :sub:`0021high`\ (bits 3--0)
    * - start + 28:
      - G\ :sub:`0023low`\ (bits 7--6)

        B\ :sub:`0022high`\ (bits 5--0)
      - G\ :sub:`0023high`
      - B\ :sub:`0024low`
      - B\ :sub:`0024high`\ (bits 1--0)
    * - start + 32:
      - G\ :sub:`0100low`
      - R\ :sub:`0101low`\ (bits 7--2)

        G\ :sub:`0100high`\ (bits 1--0)
      - G\ :sub:`0102low`\ (bits 7--4)

        R\ :sub:`0101high`\ (bits 3--0)
      - R\ :sub:`0103low`\ (bits 7--6)

        G\ :sub:`0102high`\ (bits 5--0)
    * - start + 36:
      - R\ :sub:`0103high`
      - G\ :sub:`0104low`
      - R\ :sub:`0105low`\ (bits 7--2)

        G\ :sub:`0104high`\ (bits 1--0)
      - G\ :sub:`0106low`\ (bits 7--4)

        R\ :sub:`0105high`\ (bits 3--0)
    * - start + 40:
      - R\ :sub:`0107low`\ (bits 7--6)

        G\ :sub:`0106high`\ (bits 5--0)
      - R\ :sub:`0107high`
      - G\ :sub:`0108low`
      - R\ :sub:`0109low`\ (bits 7--2)

        G\ :sub:`0108high`\ (bits 1--0)
    * - start + 44:
      - G\ :sub:`0110low`\ (bits 7--4)

        R\ :sub:`0109high`\ (bits 3--0)
      - R\ :sub:`0111low`\ (bits 7--6)

        G\ :sub:`0110high`\ (bits 5--0)
      - R\ :sub:`0111high`
      - G\ :sub:`0112low`
    * - start + 48:
      - R\ :sub:`0113low`\ (bits 7--2)

        G\ :sub:`0112high`\ (bits 1--0)
      - G\ :sub:`0114low`\ (bits 7--4)

        R\ :sub:`0113high`\ (bits 3--0)
      - R\ :sub:`0115low`\ (bits 7--6)

        G\ :sub:`0114high`\ (bits 5--0)
      - R\ :sub:`0115high`
    * - start + 52:
      - G\ :sub:`0116low`
      - R\ :sub:`0117low`\ (bits 7--2)

        G\ :sub:`0116high`\ (bits 1--0)
      - G\ :sub:`0118low`\ (bits 7--4)

        R\ :sub:`0117high`\ (bits 3--0)
      - R\ :sub:`0119low`\ (bits 7--6)

        G\ :sub:`0118high`\ (bits 5--0)
    * - start + 56:
      - R\ :sub:`0119high`
      - G\ :sub:`0120low`
      - R\ :sub:`0121low`\ (bits 7--2)

        G\ :sub:`0120high`\ (bits 1--0)
      - G\ :sub:`0122low`\ (bits 7--4)

        R\ :sub:`0121high`\ (bits 3--0)
    * - start + 60:
      - R\ :sub:`0123low`\ (bits 7--6)

        G\ :sub:`0122high`\ (bits 5--0)
      - R\ :sub:`0123high`
      - G\ :sub:`0124low`
      - G\ :sub:`0124high`\ (bits 1--0)
    * - start + 64:
      - B\ :sub:`0200low`
      - G\ :sub:`0201low`\ (bits 7--2)

        B\ :sub:`0200high`\ (bits 1--0)
      - B\ :sub:`0202low`\ (bits 7--4)

        G\ :sub:`0201high`\ (bits 3--0)
      - G\ :sub:`0203low`\ (bits 7--6)

        B\ :sub:`0202high`\ (bits 5--0)
    * - start + 68:
      - G\ :sub:`0203high`
      - B\ :sub:`0204low`
      - G\ :sub:`0205low`\ (bits 7--2)

        B\ :sub:`0204high`\ (bits 1--0)
      - B\ :sub:`0206low`\ (bits 7--4)

        G\ :sub:`0205high`\ (bits 3--0)
    * - start + 72:
      - G\ :sub:`0207low`\ (bits 7--6)

        B\ :sub:`0206high`\ (bits 5--0)
      - G\ :sub:`0207high`
      - B\ :sub:`0208low`
      - G\ :sub:`0209low`\ (bits 7--2)

        B\ :sub:`0208high`\ (bits 1--0)
    * - start + 76:
      - B\ :sub:`0210low`\ (bits 7--4)

        G\ :sub:`0209high`\ (bits 3--0)
      - G\ :sub:`0211low`\ (bits 7--6)

        B\ :sub:`0210high`\ (bits 5--0)
      - G\ :sub:`0211high`
      - B\ :sub:`0212low`
    * - start + 80:
      - G\ :sub:`0213low`\ (bits 7--2)

        B\ :sub:`0212high`\ (bits 1--0)
      - B\ :sub:`0214low`\ (bits 7--4)

        G\ :sub:`0213high`\ (bits 3--0)
      - G\ :sub:`0215low`\ (bits 7--6)

        B\ :sub:`0214high`\ (bits 5--0)
      - G\ :sub:`0215high`
    * - start + 84:
      - B\ :sub:`0216low`
      - G\ :sub:`0217low`\ (bits 7--2)

        B\ :sub:`0216high`\ (bits 1--0)
      - B\ :sub:`0218low`\ (bits 7--4)

        G\ :sub:`0217high`\ (bits 3--0)
      - G\ :sub:`0219low`\ (bits 7--6)

        B\ :sub:`0218high`\ (bits 5--0)
    * - start + 88:
      - G\ :sub:`0219high`
      - B\ :sub:`0220low`
      - G\ :sub:`0221low`\ (bits 7--2)

        B\ :sub:`0220high`\ (bits 1--0)
      - B\ :sub:`0222low`\ (bits 7--4)

        G\ :sub:`0221high`\ (bits 3--0)
    * - start + 92:
      - G\ :sub:`0223low`\ (bits 7--6)

        B\ :sub:`0222high`\ (bits 5--0)
      - G\ :sub:`0223high`
      - B\ :sub:`0224low`
      - B\ :sub:`0224high`\ (bits 1--0)
    * - start + 96:
      - G\ :sub:`0300low`
      - R\ :sub:`0301low`\ (bits 7--2)

        G\ :sub:`0300high`\ (bits 1--0)
      - G\ :sub:`0302low`\ (bits 7--4)

        R\ :sub:`0301high`\ (bits 3--0)
      - R\ :sub:`0303low`\ (bits 7--6)

        G\ :sub:`0302high`\ (bits 5--0)
    * - start + 100:
      - R\ :sub:`0303high`
      - G\ :sub:`0304low`
      - R\ :sub:`0305low`\ (bits 7--2)

        G\ :sub:`0304high`\ (bits 1--0)
      - G\ :sub:`0306low`\ (bits 7--4)

        R\ :sub:`0305high`\ (bits 3--0)
    * - start + 104:
      - R\ :sub:`0307low`\ (bits 7--6)

        G\ :sub:`0306high`\ (bits 5--0)
      - R\ :sub:`0307high`
      - G\ :sub:`0308low`
      - R\ :sub:`0309low`\ (bits 7--2)

        G\ :sub:`0308high`\ (bits 1--0)
    * - start + 108:
      - G\ :sub:`0310low`\ (bits 7--4)

        R\ :sub:`0309high`\ (bits 3--0)
      - R\ :sub:`0311low`\ (bits 7--6)

        G\ :sub:`0310high`\ (bits 5--0)
      - R\ :sub:`0311high`
      - G\ :sub:`0312low`
    * - start + 112:
      - R\ :sub:`0313low`\ (bits 7--2)

        G\ :sub:`0312high`\ (bits 1--0)
      - G\ :sub:`0314low`\ (bits 7--4)

        R\ :sub:`0313high`\ (bits 3--0)
      - R\ :sub:`0315low`\ (bits 7--6)

        G\ :sub:`0314high`\ (bits 5--0)
      - R\ :sub:`0315high`
    * - start + 116:
      - G\ :sub:`0316low`
      - R\ :sub:`0317low`\ (bits 7--2)

        G\ :sub:`0316high`\ (bits 1--0)
      - G\ :sub:`0318low`\ (bits 7--4)

        R\ :sub:`0317high`\ (bits 3--0)
      - R\ :sub:`0319low`\ (bits 7--6)

        G\ :sub:`0318high`\ (bits 5--0)
    * - start + 120:
      - R\ :sub:`0319high`
      - G\ :sub:`0320low`
      - R\ :sub:`0321low`\ (bits 7--2)

        G\ :sub:`0320high`\ (bits 1--0)
      - G\ :sub:`0322low`\ (bits 7--4)

        R\ :sub:`0321high`\ (bits 3--0)
    * - start + 124:
      - R\ :sub:`0323low`\ (bits 7--6)

        G\ :sub:`0322high`\ (bits 5--0)
      - R\ :sub:`0323high`
      - G\ :sub:`0324low`
      - G\ :sub:`0324high`\ (bits 1--0)
