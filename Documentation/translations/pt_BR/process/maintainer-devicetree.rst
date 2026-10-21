.. SPDX-License-Identifier: GPL-2.0

=========================================
Subsistema de Devicetree e Open Firmware
=========================================

Outros documentos sobre o processo
----------------------------------

Consulte os documentos em Documentation/devicetree/bindings/ para saber como
escrever bindings de Devicetree corretamente e como enviar patches.

Revisão e tratamento de patches
-------------------------------

Os patches sob responsabilidade dos mantenedores de Devicetree são processados
de formas distintas, conforme o tipo de patch:

1. Código central de drivers OF, por exemplo, drivers/of/:
   os patches são revisados e aplicados pelos mantenedores de DT.

2. Bindings de Devicetree:
   os patches são revisados pelos mantenedores de DT, mas devem ser aplicados
   pelos mantenedores do subsistema, exceto em alguns casos. Consulte também
   *Para mantenedores do kernel* em
   Documentation/devicetree/bindings/submitting-patches.rst.

3. DTS e drivers:
   os mantenedores de DT podem fazer comentários, mas, em geral, não se espera
   uma revisão. Os DTS devem passar nas verificações de esquema
   (dtbs_check) ou, ao menos, não gerar novos avisos.

Patchwork
~~~~~~~~~

Os mantenedores de Devicetree revisam patches usando o Patchwork; portanto, o
status atual de um patch pode ser consultado por lá. Em submissões típicas de
drivers, o Patchwork recebe toda a série de patches, mas normalmente apenas
alguns patches são bindings de Devicetree e, assim, revisados pelos
mantenedores de DT.

Explicação dos status do Patchwork:

 - **New**: ainda não processado pelo conjunto de ferramentas de automação.
 - **Needs ACK**: aguardando revisão dos mantenedores de DT.
 - **Handled Elsewhere**: patch não relacionado a DT; não será revisado aqui.
 - **RFC**: o patch provavelmente foi ignorado por ser um RFC incompleto.
 - **Changes Requested**: o patch foi revisado e os mantenedores de DT esperam
   alterações.
 - **Accepted**: o patch foi revisado e aplicado pelos mantenedores de DT em
   sua árvore.
 - **Not Applicable**: o patch foi revisado e provavelmente está em boas
   condições, com uma tag *Reviewed-by* ou *Acked-by* fornecida, mas os
   mantenedores de DT esperam que outra pessoa o aplique.

Nova revisão e pings de patches
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Devido ao alto volume de e-mails, os mantenedores de Devicetree não leem todas
as mensagens que recebem; em vez disso, eles dependem do Patchwork durante o
processo de revisão. Além disso, muitas vezes deixam de lado patches que já
foram revisados.

Como resultado, os mantenedores podem não perceber:

1. Perguntas sobre patches já revisados.
2. Pings, por exemplo, quando um patch foi revisado pelos mantenedores de DT,
   mas ainda não foi aplicado pelos mantenedores do subsistema.

Esses casos podem ser tratados das seguintes formas:

1. Enviando um ping aos mantenedores de DT no canal de IRC.
2. Removendo a tag *Acked-by* ou *Reviewed-by* do mantenedor de DT ao enviar
   uma nova versão da série de patches, junto com uma explicação no changelog
   do patch sobre o motivo da remoção da tag e o que se espera dos mantenedores
   de DT.
