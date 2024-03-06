.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-sp.rst

:Original: Documentation/process/security-bugs.rst
:Translator: Avadhut Naik <avadhut.naik@amd.com>

Errores de seguridad
====================

Los desarrolladores del kernel de Linux se toman la seguridad muy en
serio. Como tal, nos gustaría saber cuándo se encuentra un error de
seguridad para que pueda ser corregido y divulgado lo más rápido posible.
Por favor, informe sobre los errores de seguridad al equipo de seguridad
del kernel de Linux.

Contacto
--------

El equipo de seguridad del kernel de Linux puede ser contactado por correo
electrónico en <security@kernel.org>. Esta es una lista privada de
oficiales de seguridad que ayudarán a verificar el informe del error y
desarrollarán y publicarán una corrección. Si ya tiene una corrección, por
favor, inclúyala con su informe, ya que eso puede acelerar considerablemente
el proceso. Es posible que el equipo de seguridad traiga ayuda adicional
de mantenedores del área para comprender y corregir la vulnerabilidad de
seguridad.

Como ocurre con cualquier error, cuanta más información se proporcione,
más fácil será diagnosticarlo y corregirlo. Por favor, revise el
procedimiento descrito en 'Documentation/admin-guide/reporting-issues.rst'
si no tiene claro que información es útil. Cualquier código de explotación
es muy útil y no será divulgado sin el consentimiento del "reportero" (el
que envia el error) a menos que ya se haya hecho público.

Por favor, envíe correos electrónicos en texto plano sin archivos
adjuntos cuando sea posible. Es mucho más difícil tener una discusión
citada en contexto sobre un tema complejo si todos los detalles están
ocultos en archivos adjuntos. Piense en ello como un
:doc:`envío de parche regular <submitting-patches>` (incluso si no tiene
un parche todavía) describa el problema y el impacto, enumere los pasos
de reproducción, y sígalo con una solución propuesta, todo en texto plano.


Divulgación e información embargada
-----------------------------------

La lista de seguridad no es un canal de divulgación. Para eso, ver
Coordinación debajo. Una vez que se ha desarrollado una solución robusta,
comienza el proceso de lanzamiento. Las soluciones para errores conocidos
públicamente se lanzan inmediatamente.

Aunque nuestra preferencia es lanzar soluciones para errores no divulgados
públicamente tan pronto como estén disponibles, esto puede postponerse a
petición del reportero o una parte afectada por hasta 7 días calendario
desde el inicio del proceso de lanzamiento, con una extensión excepcional
a 14 días de calendario si se acuerda que la criticalidad del error requiere
más tiempo. La única razón válida para aplazar la publicación de una
solución es para acomodar la logística de QA y los despliegues a gran
escala que requieren coordinación de lanzamiento.

Si bien la información embargada puede compartirse con personas de
confianza para desarrollar una solución, dicha información no se publicará
junto con la solución o en cualquier otro canal de divulgación sin el
permiso del reportero. Esto incluye, pero no se limita al informe original
del error y las discusiones de seguimiento (si las hay), exploits,
información sobre CVE o la identidad del reportero.

En otras palabras, nuestro único interés es solucionar los errores. Toda
otra información presentada a la lista de seguridad y cualquier discusión
de seguimiento del informe se tratan confidencialmente incluso después de
que se haya levantado el embargo, en perpetuidad.

Coordinación con otros grupos
-----------------------------

El equipo de seguridad del kernel recomienda encarecidamente que los
reporteros de posibles problemas de seguridad NUNCA contacten la lista
de correo “linux-distros” hasta DESPUES de discutirlo con el equipo de
seguridad del kernel. No Cc: ambas listas a la vez. Puede ponerse en
contacto con la lista de correo linux-distros después de que se haya
acordado una solución y comprenda completamente los requisitos que al
hacerlo le impondrá a usted y la comunidad del kernel.

Las diferentes listas tienen diferentes objetivos y las reglas de
linux-distros no contribuyen en realidad a solucionar ningún problema de
seguridad potencial.

Asignación de CVE
-----------------

El equipo de seguridad no asigna CVEs, ni los requerimos para informes o
correcciones, ya que esto puede complicar innecesariamente el proceso y
puede retrasar el manejo de errores. Si un reportero desea que se le
asigne un identificador CVE, debe buscar uno por sí mismo, por ejemplo,
poniéndose en contacto directamente con MITRE. Sin embargo, en ningún
caso se retrasará la inclusión de un parche para esperar a que llegue un
identificador CVE.

Acuerdos de no divulgación
--------------------------

El equipo de seguridad del kernel de Linux no es un organismo formal y,
por lo tanto, no puede firmar cualquier acuerdo de no divulgación.
